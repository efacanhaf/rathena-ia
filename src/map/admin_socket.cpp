// DimensionsRO — admin command bridge over a localhost TCP socket.
//
// Protocol (line-based, LF-terminated):
//   client> AUTH <token>
//   server< OK auth        | ERR <reason>
//   client> EXEC <cmd> [args]
//   server< OK queued      | ERR <reason>
//   <connection closes>
//
// Listener runs on a dedicated worker thread (raw POSIX/Winsock — does not
// share rAthena's per-fd session table). Validated commands are pushed onto
// a queue and executed by a 200ms timer in the main thread, which is the
// only place is_atcommand() may be called safely.

#include "admin_socket.hpp"

#include <common/cbasetypes.hpp>
#include <common/malloc.hpp>
#include <common/showmsg.hpp>
#include <common/strlib.hpp>
#include <common/timer.hpp>

#include "atcommand.hpp"
#include "map.hpp"
#include "pc.hpp"
#include "pc_groups.hpp"

// rAthena's wrapper handles winsock include ordering. We reuse it on Windows
// so we don't drag in raw winsock2.h before rAthena's headers and break the
// type definitions (uint16 vs winsock typedefs, etc).
#ifdef WIN32
#include <common/winapi.hpp>
#define ADMIN_CLOSE(s) closesocket(s)
#define ADMIN_SHUTDOWN(s) shutdown((s), SD_BOTH)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define ADMIN_CLOSE(s) close(s)
#define ADMIN_SHUTDOWN(s) shutdown((s), SHUT_RDWR)
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Whitelist — only safe @reload-style commands. Anything not here is rejected.
// "event" is allowed so the Discord cog can auto-chain `@event refresh` after
// `@reloadbattleconf`, keeping the dro_event_manager NPC's cached baseline in
// sync with battle_conf when an event is active.
static const std::set<std::string> ADMIN_WHITELIST = {
    "reloadbattleconf",
    "reloaditemdb",
    "reloadmobdb",
    "reloadskilldb",
    "reloadscript",
    "reloadatcommand",
    "reloadstatusdb",
    "loadnpc",
    "reloadbarterdb",
    "reloadquestdb",
    "reloadmsgconf",
    "reloadnpc",
    "reloadachievementdb",
    "reloadinstancedb",
    "event",
};

struct AdminTask {
	std::string command;
	std::string args;
};

static std::atomic<bool> server_running{false};
static std::thread server_thread;
static SOCKET listen_sock = INVALID_SOCKET;

static std::mutex queue_mtx;
static std::queue<AdminTask> task_queue;

static std::string admin_token;
static uint16 admin_port = 7000;
static std::string admin_bind = "127.0.0.1";

// Persistent dummy session shared across all admin-issued atcommands. Mirrors
// what script.cpp does for the BUILDIN(atcommand) path: heap-allocated, with a
// loaded permission group, so reload commands that walk into pc_get_group_level
// or other sd->group lookups don't deref a null group pointer and crash.
static map_session_data* admin_dummy_sd = nullptr;

static void ensure_admin_dummy_sd() {
	if (admin_dummy_sd != nullptr)
		return;
	CREATE(admin_dummy_sd, map_session_data, 1);
	new (admin_dummy_sd) map_session_data();
	safestrncpy(admin_dummy_sd->status.name, "AdminBridge", NAME_LENGTH);
	admin_dummy_sd->group_id = 99;
	admin_dummy_sd->fd = 0;
	pc_group_pc_load(admin_dummy_sd);
}

static int recv_line(SOCKET s, char* buf, size_t maxlen) {
	size_t i = 0;
	while (i + 1 < maxlen) {
		char c;
		int n = (int)recv(s, &c, 1, 0);
		if (n <= 0)
			return -1;
		if (c == '\r')
			continue;
		if (c == '\n')
			break;
		buf[i++] = c;
	}
	buf[i] = '\0';
	return (int)i;
}

static void send_line(SOCKET s, const char* line) {
	send(s, line, (int)strlen(line), 0);
	send(s, "\n", 1, 0);
}

static bool is_whitelisted(const std::string& cmd) {
	return ADMIN_WHITELIST.count(cmd) > 0;
}

static void handle_client(SOCKET cs) {
	char buf[512];

	if (recv_line(cs, buf, sizeof(buf)) < 0) {
		ADMIN_CLOSE(cs);
		return;
	}
	if (strncmp(buf, "AUTH ", 5) != 0 || admin_token != (buf + 5)) {
		send_line(cs, "ERR auth");
		ADMIN_CLOSE(cs);
		return;
	}
	send_line(cs, "OK auth");

	if (recv_line(cs, buf, sizeof(buf)) < 0) {
		ADMIN_CLOSE(cs);
		return;
	}
	if (strncmp(buf, "EXEC ", 5) != 0) {
		send_line(cs, "ERR proto");
		ADMIN_CLOSE(cs);
		return;
	}

	std::string line(buf + 5);
	size_t sp = line.find(' ');
	std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
	std::string args = (sp == std::string::npos) ? "" : line.substr(sp + 1);

	if (cmd.empty() || !is_whitelisted(cmd)) {
		send_line(cs, "ERR not_whitelisted");
		ADMIN_CLOSE(cs);
		return;
	}

	{
		std::lock_guard<std::mutex> lg(queue_mtx);
		task_queue.push({cmd, args});
	}
	send_line(cs, "OK queued");
	ADMIN_CLOSE(cs);
}

static void server_loop() {
	while (server_running.load()) {
		struct sockaddr_in cli;
		socklen_t cl = sizeof(cli);
		SOCKET cs = accept(listen_sock, (struct sockaddr*)&cli, &cl);
		if (cs == INVALID_SOCKET) {
			if (!server_running.load())
				break;
			continue;
		}
		// belt + suspenders — bind already restricts to 127.0.0.1
		if (cli.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
			ADMIN_CLOSE(cs);
			continue;
		}
		handle_client(cs);
	}
}

static TIMER_FUNC(admin_drain_queue) {
	std::vector<AdminTask> drained;
	{
		std::lock_guard<std::mutex> lg(queue_mtx);
		while (!task_queue.empty()) {
			drained.push_back(std::move(task_queue.front()));
			task_queue.pop();
		}
	}
	if (!drained.empty())
		ensure_admin_dummy_sd();
	for (auto& t : drained) {
		std::string at_msg = "@" + t.command;
		if (!t.args.empty()) {
			at_msg += " ";
			at_msg += t.args;
		}
		ShowInfo("[admin_socket] executing '%s'\n", at_msg.c_str());
		is_atcommand(admin_dummy_sd->fd, admin_dummy_sd, at_msg.c_str(), 2);
	}
	return 0;
}

static bool load_conf() {
	const char* path = "conf/import/admin_socket.conf";
	FILE* fp = fopen(path, "r");
	if (fp == nullptr) {
		ShowInfo("[admin_socket] %s not found, admin bridge disabled.\n", path);
		return false;
	}
	char line[1024], k[64], v[960];
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '/' || line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, "%63[^:]:%959[^\n]", k, v) < 2)
			continue;
		char* vp = v;
		while (*vp == ' ' || *vp == '\t')
			vp++;
		char* end = vp + strlen(vp);
		while (end > vp && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
			*--end = '\0';
		if (strcmp(k, "token") == 0)
			admin_token = vp;
		else if (strcmp(k, "port") == 0)
			admin_port = (uint16)atoi(vp);
		else if (strcmp(k, "bind_ip") == 0)
			admin_bind = vp;
	}
	fclose(fp);
	if (admin_token.empty() || admin_token == "CHANGE_ME") {
		ShowWarning("[admin_socket] token not set in %s, disabling.\n", path);
		return false;
	}
	return true;
}

void do_init_admin_socket() {
#ifdef MAP_GENERATOR
	return;
#else
	if (!load_conf())
		return;

#ifdef WIN32
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) {
		ShowError("[admin_socket] socket() failed\n");
		return;
	}
	int yes = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(admin_port);
	addr.sin_addr.s_addr = inet_addr(admin_bind.c_str());
	if (addr.sin_addr.s_addr == INADDR_NONE) {
		ShowError("[admin_socket] invalid bind_ip '%s'\n", admin_bind.c_str());
		ADMIN_CLOSE(listen_sock);
		listen_sock = INVALID_SOCKET;
		return;
	}

	if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
	    listen(listen_sock, 4) == SOCKET_ERROR) {
		ShowError("[admin_socket] bind/listen on %s:%u failed\n",
		          admin_bind.c_str(), admin_port);
		ADMIN_CLOSE(listen_sock);
		listen_sock = INVALID_SOCKET;
		return;
	}

	server_running = true;
	server_thread = std::thread(server_loop);

	add_timer_func_list(admin_drain_queue, "admin_drain_queue");
	add_timer_interval(gettick() + 200, admin_drain_queue, 0, 0, 200);

	ShowStatus("Admin socket listening on " CL_WHITE "%s:%u" CL_RESET ".\n",
	           admin_bind.c_str(), admin_port);
#endif
}

void do_final_admin_socket() {
	if (server_running.load()) {
		server_running = false;
		if (listen_sock != INVALID_SOCKET) {
			ADMIN_SHUTDOWN(listen_sock);
			ADMIN_CLOSE(listen_sock);
			listen_sock = INVALID_SOCKET;
		}
		if (server_thread.joinable())
			server_thread.join();
	}
	if (admin_dummy_sd != nullptr) {
		admin_dummy_sd->~map_session_data();
		aFree(admin_dummy_sd);
		admin_dummy_sd = nullptr;
	}
}
