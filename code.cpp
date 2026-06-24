/*
 * Disaster Response Management System - Backend Server
 * Compile with MinGW: g++ -o server server.cpp -lws2_32
 * Run: server.exe
 * Listens on http://localhost:8080
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <stack>
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

// ============================================================
//  DATA STRUCTURES
// ============================================================

struct Zone {
    int id;
    std::string name;
    int severity;      // 1-10 (10 = most critical)
    int population;
    std::string type;  // flood / earthquake / fire / landslide
    bool rescued;
    std::string assignedTeam;
    std::string timestamp;
};

struct Team {
    int id;
    std::string name;
    std::string type;  // medical / rescue / engineering / fire
    bool available;
    std::string currentZone;
};

struct Edge {
    int to;
    int weight; // distance/time in minutes
};

struct OperationLog {
    std::string action;
    std::string detail;
    std::string timestamp;
};

// ============================================================
//  GLOBAL STATE
// ============================================================

// Graph adjacency list (location graph)
std::map<int, std::vector<Edge> > graph;
std::map<int, std::string>        locationNames;

// Priority queue for zones (max-heap by severity)
struct ZonePriority {
    int severity;
    int id;
    bool operator<(const ZonePriority& o) const {
        return severity < o.severity; // max-heap
    }
};
std::priority_queue<ZonePriority> rescueQueue;

// All zones and teams
std::map<int, Zone> zones;
std::map<int, Team> teams;

// Operation history stack
std::stack<OperationLog> opHistory;

// Tree structure: command hierarchy
// nodeId -> list of child nodeIds
std::map<int, std::vector<int> > commandTree;
std::map<int, std::string>       commandNames;
std::map<int, int>               commandParent;

// ID counters
int nextZoneId = 1;
int nextTeamId = 1;

// ============================================================
//  HELPERS
// ============================================================

std::string currentTime() {
    time_t now = time(0);
    char buf[64];
    struct tm* t = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return std::string(buf);
}

void pushLog(const std::string& action, const std::string& detail) {
    OperationLog log;
    log.action    = action;
    log.detail    = detail;
    log.timestamp = currentTime();
    opHistory.push(log);
}

// Escape JSON strings
std::string jesc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

// ============================================================
//  SEED DATA
// ============================================================

void seedData() {
    // --- Locations (graph nodes) ---
    locationNames[0] = "Command HQ";
    locationNames[1] = "North District";
    locationNames[2] = "South District";
    locationNames[3] = "East Block";
    locationNames[4] = "West Block";
    locationNames[5] = "River Zone";
    locationNames[6] = "Hill Area";
    locationNames[7] = "Industrial";
    locationNames[8] = "Coastal Area";

    // --- Graph edges (bidirectional) ---
    auto addEdge = [](int u, int v, int w) {
        graph[u].push_back({v, w});
        graph[v].push_back({u, w});
    };
    addEdge(0, 1, 10);
    addEdge(0, 2, 12);
    addEdge(1, 3, 8);
    addEdge(1, 5, 15);
    addEdge(2, 4, 9);
    addEdge(2, 8, 20);
    addEdge(3, 6, 11);
    addEdge(4, 7, 7);
    addEdge(5, 6, 5);
    addEdge(6, 7, 13);
    addEdge(7, 8, 18);

    // --- Command Tree ---
    commandNames[0]  = "Director General";
    commandNames[1]  = "Field Commander North";
    commandNames[2]  = "Field Commander South";
    commandNames[3]  = "Medical Lead";
    commandNames[4]  = "Rescue Lead";
    commandNames[5]  = "Logistics Lead";
    commandNames[6]  = "Team Alpha";
    commandNames[7]  = "Team Bravo";
    commandNames[8]  = "Team Charlie";
    commandNames[9]  = "Team Delta";
    commandNames[10] = "Team Echo";

    commandParent[1]  = 0; commandTree[0].push_back(1);
    commandParent[2]  = 0; commandTree[0].push_back(2);
    commandParent[3]  = 1; commandTree[1].push_back(3);
    commandParent[4]  = 1; commandTree[1].push_back(4);
    commandParent[5]  = 2; commandTree[2].push_back(5);
    commandParent[6]  = 3; commandTree[3].push_back(6);
    commandParent[7]  = 3; commandTree[3].push_back(7);
    commandParent[8]  = 4; commandTree[4].push_back(8);
    commandParent[9]  = 4; commandTree[4].push_back(9);
    commandParent[10] = 5; commandTree[5].push_back(10);

    // --- Teams ---
    std::string teamNames[] = {"Alpha", "Bravo", "Charlie", "Delta", "Echo"};
    std::string teamTypes[] = {"rescue", "medical", "engineering", "fire", "rescue"};
    for (int i = 0; i < 5; i++) {
        Team t;
        t.id          = nextTeamId++;
        t.name        = "Team " + teamNames[i];
        t.type        = teamTypes[i];
        t.available   = true;
        t.currentZone = "HQ";
        teams[t.id]   = t;
    }

    // --- Initial Zones ---
    std::string zNames[]  = {"River Zone Flood", "Hill Collapse", "Coastal Surge", "Industrial Fire"};
    std::string zTypes[]  = {"flood", "earthquake", "flood", "fire"};
    int         zSev[]    = {9, 7, 8, 6};
    int         zPop[]    = {1200, 450, 800, 200};

    for (int i = 0; i < 4; i++) {
        Zone z;
        z.id          = nextZoneId++;
        z.name        = zNames[i];
        z.severity    = zSev[i];
        z.population  = zPop[i];
        z.type        = zTypes[i];
        z.rescued     = false;
        z.assignedTeam= "";
        z.timestamp   = currentTime();
        zones[z.id]   = z;
        rescueQueue.push({z.severity, z.id});
    }

    pushLog("SYSTEM", "Disaster Response System initialised");
}

// ============================================================
//  BFS SHORTEST PATH
// ============================================================

struct PathResult {
    bool   found;
    int    distance;
    std::vector<int> path;
};

PathResult bfsShortestPath(int src, int dst) {
    PathResult result;
    result.found    = false;
    result.distance = -1;

    if (locationNames.find(src) == locationNames.end() ||
        locationNames.find(dst) == locationNames.end()) {
        return result;
    }

    // Dijkstra (weighted BFS)
    std::map<int, int> dist;
    std::map<int, int> prev;
    for (auto& kv : locationNames) { dist[kv.first] = 999999; }
    dist[src] = 0;

    // min-heap: (dist, node)
    std::priority_queue<std::pair<int,int>,
                        std::vector<std::pair<int,int> >,
                        std::greater<std::pair<int,int> > > pq;
    pq.push(std::make_pair(0, src));

    while (!pq.empty()) {
        int d = pq.top().first;
        int u = pq.top().second;
        pq.pop();
        if (d > dist[u]) continue;
        if (graph.find(u) == graph.end()) continue;
        for (size_t i = 0; i < graph[u].size(); i++) {
            int v = graph[u][i].to;
            int w = graph[u][i].weight;
            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                prev[v] = u;
                pq.push(std::make_pair(dist[v], v));
            }
        }
    }

    if (dist[dst] == 999999) return result;

    result.found    = true;
    result.distance = dist[dst];

    // Reconstruct path
    int cur = dst;
    while (cur != src) {
        result.path.push_back(cur);
        cur = prev[cur];
    }
    result.path.push_back(src);
    std::reverse(result.path.begin(), result.path.end());
    return result;
}

// ============================================================
//  JSON BUILDERS
// ============================================================

std::string zonesJson() {
    // Rebuild priority queue snapshot (sorted copy)
    std::vector<Zone> sorted;
    for (auto& kv : zones) sorted.push_back(kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const Zone& a, const Zone& b){
        return a.severity > b.severity;
    });

    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < sorted.size(); i++) {
        const Zone& z = sorted[i];
        if (i) o << ",";
        o << "{"
          << "\"id\":"          << z.id          << ","
          << "\"name\":\""      << jesc(z.name)  << "\","
          << "\"severity\":"    << z.severity    << ","
          << "\"population\":"  << z.population  << ","
          << "\"type\":\""      << jesc(z.type)  << "\","
          << "\"rescued\":"     << (z.rescued ? "true" : "false") << ","
          << "\"assignedTeam\":\"" << jesc(z.assignedTeam) << "\","
          << "\"timestamp\":\"" << jesc(z.timestamp) << "\""
          << "}";
    }
    o << "]";
    return o.str();
}

std::string teamsJson() {
    std::ostringstream o;
    o << "[";
    bool first = true;
    for (auto& kv : teams) {
        const Team& t = kv.second;
        if (!first) o << ",";
        first = false;
        o << "{"
          << "\"id\":"           << t.id           << ","
          << "\"name\":\""       << jesc(t.name)   << "\","
          << "\"type\":\""       << jesc(t.type)   << "\","
          << "\"available\":"    << (t.available ? "true" : "false") << ","
          << "\"currentZone\":\"" << jesc(t.currentZone) << "\""
          << "}";
    }
    o << "]";
    return o.str();
}

std::string logsJson() {
    // Copy stack to vector (top = most recent)
    std::stack<OperationLog> tmp = opHistory;
    std::vector<OperationLog> logs;
    while (!tmp.empty()) {
        logs.push_back(tmp.top());
        tmp.pop();
    }
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < logs.size(); i++) {
        if (i) o << ",";
        o << "{"
          << "\"action\":\""    << jesc(logs[i].action)    << "\","
          << "\"detail\":\""    << jesc(logs[i].detail)    << "\","
          << "\"timestamp\":\"" << jesc(logs[i].timestamp) << "\""
          << "}";
    }
    o << "]";
    return o.str();
}

std::string graphJson() {
    std::ostringstream o;
    o << "{\"nodes\":[";
    bool first = true;
    for (auto& kv : locationNames) {
        if (!first) o << ",";
        first = false;
        o << "{\"id\":" << kv.first << ",\"name\":\"" << jesc(kv.second) << "\"}";
    }
    o << "],\"edges\":[";
    first = true;
    std::set<std::pair<int,int> > seen;
    for (auto& kv : graph) {
        for (auto& e : kv.second) {
            int u = kv.first, v = e.to;
            if (u > v) std::swap(u, v);
            if (seen.count(std::make_pair(u,v))) continue;
            seen.insert(std::make_pair(u,v));
            if (!first) o << ",";
            first = false;
            o << "{\"from\":" << kv.first << ",\"to\":" << e.to << ",\"weight\":" << e.weight << "}";
        }
    }
    o << "]}";
    return o.str();
}

std::string commandTreeJson() {
    std::ostringstream o;
    o << "{\"nodes\":[";
    bool first = true;
    for (auto& kv : commandNames) {
        if (!first) o << ",";
        first = false;
        int parent = -1;
        if (commandParent.find(kv.first) != commandParent.end())
            parent = commandParent[kv.first];
        o << "{\"id\":" << kv.first
          << ",\"name\":\"" << jesc(kv.second) << "\""
          << ",\"parent\":" << parent << "}";
    }
    o << "]}";
    return o.str();
}

std::string statsJson() {
    int total   = (int)zones.size();
    int rescued = 0;
    int active  = 0;
    int avail   = 0;
    int totalPop = 0;
    for (auto& kv : zones) {
        if (kv.second.rescued) rescued++;
        else active++;
        totalPop += kv.second.population;
    }
    for (auto& kv : teams) {
        if (kv.second.available) avail++;
    }
    std::ostringstream o;
    o << "{"
      << "\"totalZones\":"    << total     << ","
      << "\"rescuedZones\":"  << rescued   << ","
      << "\"activeZones\":"   << active    << ","
      << "\"totalTeams\":"    << (int)teams.size() << ","
      << "\"availableTeams\":" << avail    << ","
      << "\"totalPopulation\":" << totalPop
      << "}";
    return o.str();
}

// ============================================================
//  REQUEST PARSING
// ============================================================

std::string getBody(const std::string& request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return request.substr(pos + 4);
}

std::string getJsonField(const std::string& json, const std::string& key) {
    // Simple extractor for "key": value or "key": "value"
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos + search.size());
    if (pos == std::string::npos) return "";
    pos++; // skip ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') pos++; // skip escape
            val += json[pos++];
        }
        return val;
    } else {
        std::string val;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
            val += json[pos++];
        }
        // trim
        while (!val.empty() && (val.back() == ' ' || val.back() == '\r' || val.back() == '\n'))
            val.pop_back();
        return val;
    }
}

std::string getQueryParam(const std::string& path, const std::string& key) {
    size_t q = path.find('?');
    if (q == std::string::npos) return "";
    std::string qs = path.substr(q + 1);
    std::string search = key + "=";
    size_t pos = qs.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = qs.find('&', pos);
    if (end == std::string::npos) end = qs.size();
    return qs.substr(pos, end - pos);
}

// ============================================================
//  HTTP RESPONSE HELPERS
// ============================================================

std::string httpResponse(int code, const std::string& body, const std::string& ct = "application/json") {
    std::string status;
    if      (code == 200) status = "200 OK";
    else if (code == 201) status = "201 Created";
    else if (code == 400) status = "400 Bad Request";
    else if (code == 404) status = "404 Not Found";
    else                  status = "500 Internal Server Error";

    std::ostringstream r;
    r << "HTTP/1.1 " << status << "\r\n"
      << "Content-Type: " << ct << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
    return r.str();
}

std::string okJson(const std::string& json) {
    return httpResponse(200, json);
}

std::string errJson(const std::string& msg) {
    return httpResponse(400, "{\"error\":\"" + jesc(msg) + "\"}");
}

// ============================================================
//  ROUTE HANDLER
// ============================================================

std::string handleRequest(const std::string& method, const std::string& rawPath, const std::string& body) {
    // Strip query string for route matching
    std::string path = rawPath;
    size_t q = path.find('?');
    std::string pathOnly = (q != std::string::npos) ? path.substr(0, q) : path;

    // CORS preflight
    if (method == "OPTIONS") {
        return httpResponse(200, "");
    }

    // GET /api/stats
    if (method == "GET" && pathOnly == "/api/stats") {
        return okJson(statsJson());
    }

    // GET /api/zones
    if (method == "GET" && pathOnly == "/api/zones") {
        return okJson(zonesJson());
    }

    // POST /api/zones  — add a new danger zone
    if (method == "POST" && pathOnly == "/api/zones") {
        std::string name     = getJsonField(body, "name");
        std::string sevStr   = getJsonField(body, "severity");
        std::string popStr   = getJsonField(body, "population");
        std::string type     = getJsonField(body, "type");
        if (name.empty() || sevStr.empty() || type.empty())
            return errJson("Missing fields: name, severity, type");

        Zone z;
        z.id          = nextZoneId++;
        z.name        = name;
        z.severity    = atoi(sevStr.c_str());
        z.population  = popStr.empty() ? 0 : atoi(popStr.c_str());
        z.type        = type;
        z.rescued     = false;
        z.assignedTeam= "";
        z.timestamp   = currentTime();
        zones[z.id]   = z;
        rescueQueue.push({z.severity, z.id});
        pushLog("ADD_ZONE", "Added zone: " + name + " (severity " + sevStr + ")");

        std::ostringstream o;
        o << "{\"id\":" << z.id << ",\"message\":\"Zone added\"}";
        return httpResponse(201, o.str());
    }

    // PUT /api/zones/rescue  — mark top-priority zone as rescued
    if (method == "PUT" && pathOnly == "/api/zones/rescue") {
        // Pop from priority queue until we find an unrescued zone
        while (!rescueQueue.empty()) {
            int id = rescueQueue.top().id;
            rescueQueue.pop();
            if (zones.find(id) != zones.end() && !zones[id].rescued) {
                zones[id].rescued = true;
                std::string teamName = getJsonField(body, "team");
                if (!teamName.empty()) zones[id].assignedTeam = teamName;
                pushLog("RESCUE", "Rescued zone: " + zones[id].name);
                std::ostringstream o;
                o << "{\"id\":" << id << ",\"name\":\"" << jesc(zones[id].name) << "\",\"message\":\"Zone rescued\"}";
                return okJson(o.str());
            }
        }
        return errJson("No active zones in queue");
    }

    // PUT /api/zones/:id/assign  — assign a team to a zone
    if (method == "PUT" && pathOnly.size() > 11 && pathOnly.substr(0, 10) == "/api/zones") {
        // /api/zones/{id}/assign
        size_t slash = pathOnly.find('/', 11);
        if (slash != std::string::npos) {
            int zid = atoi(pathOnly.substr(10, slash - 10).c_str());
            std::string teamName = getJsonField(body, "team");
            int teamId = atoi(getJsonField(body, "teamId").c_str());

            if (zones.find(zid) == zones.end())
                return errJson("Zone not found");
            if (teamName.empty())
                return errJson("Missing team name");

            zones[zid].assignedTeam = teamName;

            // Mark team as unavailable
            for (auto& kv : teams) {
                if (kv.second.id == teamId) {
                    kv.second.available   = false;
                    kv.second.currentZone = zones[zid].name;
                }
            }
            pushLog("ASSIGN", "Team " + teamName + " assigned to " + zones[zid].name);
            return okJson("{\"message\":\"Team assigned\"}");
        }
    }

    // GET /api/teams
    if (method == "GET" && pathOnly == "/api/teams") {
        return okJson(teamsJson());
    }

    // POST /api/teams
    if (method == "POST" && pathOnly == "/api/teams") {
        std::string name = getJsonField(body, "name");
        std::string type = getJsonField(body, "type");
        if (name.empty() || type.empty())
            return errJson("Missing fields: name, type");

        Team t;
        t.id          = nextTeamId++;
        t.name        = name;
        t.type        = type;
        t.available   = true;
        t.currentZone = "HQ";
        teams[t.id]   = t;
        pushLog("ADD_TEAM", "New team: " + name + " (" + type + ")");

        std::ostringstream o;
        o << "{\"id\":" << t.id << ",\"message\":\"Team added\"}";
        return httpResponse(201, o.str());
    }

    // PUT /api/teams/:id/release  — mark team as available
    if (method == "PUT" && pathOnly.size() > 11 && pathOnly.substr(0, 10) == "/api/teams") {
        size_t slash = pathOnly.find('/', 11);
        if (slash != std::string::npos) {
            int tid = atoi(pathOnly.substr(10, slash - 10).c_str());
            if (teams.find(tid) == teams.end())
                return errJson("Team not found");
            teams[tid].available   = true;
            teams[tid].currentZone = "HQ";
            pushLog("RELEASE", "Team " + teams[tid].name + " returned to HQ");
            return okJson("{\"message\":\"Team released\"}");
        }
    }

    // GET /api/path?from=X&to=Y
    if (method == "GET" && pathOnly == "/api/path") {
        std::string fromStr = getQueryParam(rawPath, "from");
        std::string toStr   = getQueryParam(rawPath, "to");
        if (fromStr.empty() || toStr.empty())
            return errJson("Missing from/to parameters");

        int src = atoi(fromStr.c_str());
        int dst = atoi(toStr.c_str());
        PathResult pr = bfsShortestPath(src, dst);

        if (!pr.found) {
            return okJson("{\"found\":false,\"path\":[],\"distance\":-1,\"names\":[]}");
        }

        std::ostringstream o;
        o << "{\"found\":true,\"distance\":" << pr.distance << ",\"path\":[";
        for (size_t i = 0; i < pr.path.size(); i++) {
            if (i) o << ",";
            o << pr.path[i];
        }
        o << "],\"names\":[";
        for (size_t i = 0; i < pr.path.size(); i++) {
            if (i) o << ",";
            o << "\"" << jesc(locationNames[pr.path[i]]) << "\"";
        }
        o << "]}";
        return okJson(o.str());
    }

    // GET /api/graph
    if (method == "GET" && pathOnly == "/api/graph") {
        return okJson(graphJson());
    }

    // GET /api/commandtree
    if (method == "GET" && pathOnly == "/api/commandtree") {
        return okJson(commandTreeJson());
    }

    // GET /api/logs
    if (method == "GET" && pathOnly == "/api/logs") {
        return okJson(logsJson());
    }

    // DELETE /api/logs/undo  — pop last operation (rollback)
    if (method == "DELETE" && pathOnly == "/api/logs/undo") {
        if (opHistory.empty())
            return errJson("Nothing to undo");
        OperationLog last = opHistory.top();
        opHistory.pop();
        std::ostringstream o;
        o << "{\"undone\":{\"action\":\"" << jesc(last.action)
          << "\",\"detail\":\"" << jesc(last.detail) << "\"}}";
        return okJson(o.str());
    }

    return httpResponse(404, "{\"error\":\"Not found\"}");
}

// ============================================================
//  SOCKET SERVER
// ============================================================

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }

    listen(server, 10);
    seedData();

    std::cout << "============================================\n";
    std::cout << "  Disaster Response System - Backend\n";
    std::cout << "  Listening on http://localhost:8080\n";
    std::cout << "============================================\n";

    while (true) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        // Read request
        char buf[8192] = {0};
        int received = recv(client, buf, sizeof(buf) - 1, 0);
        if (received <= 0) { closesocket(client); continue; }

        std::string request(buf, received);

        // Parse first line
        size_t lineEnd = request.find("\r\n");
        if (lineEnd == std::string::npos) { closesocket(client); continue; }
        std::string firstLine = request.substr(0, lineEnd);

        std::istringstream ss(firstLine);
        std::string method, rawPath, version;
        ss >> method >> rawPath >> version;

        std::string body = getBody(request);
        std::string response = handleRequest(method, rawPath, body);

        send(client, response.c_str(), (int)response.size(), 0);
        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
