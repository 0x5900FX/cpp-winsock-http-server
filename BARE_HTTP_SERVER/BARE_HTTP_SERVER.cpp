
#pragma comment(lib, "Ws2_32.lib")

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <print>
#include <string>
#include <sstream>
#include <unordered_map>
#include <cctype>
#include <algorithm>
#include <functional>
#include <fstream>
#include <vector>
#include <filesystem>
#include <istream>
#include <iterator>
#include <thread>
#include <chrono>

using namespace std;

namespace fs = std::filesystem;

class Wsocket {

public:
	Wsocket() {

		WSADATA wsaData;

		int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

		std::println("initialized");

		if (iResult != 0) {
			throw runtime_error("WSAStartup failed with error: " + to_string(iResult));
		}

		std::println(R"(
		Winsock version : {}.{}


		)", (int)LOBYTE(wsaData.wVersion), (int)HIBYTE(wsaData.wVersion));
	}

	~Wsocket() {

		WSACleanup();
		std::println("cleaned up");
	}
};
struct  Request
{
	string method;
	string path;
	string version;

	Request(string method, string path, string version) :
		method(method), path(path), version(version) {
	}
};
struct Response
{
	string content;
	int status_code;
	string content_Type;

	Response(string content, int status_code, string content_type) :
		content(content), status_code(status_code), content_Type(content_type) {
	}

	auto send_response() {
		string reason = (status_code == 200) ? "OK" : (status_code == 404) ? "Not Found" : (status_code == 405) ? "Method Not Allowed" : (status_code == 500) ? "Internal Server Error" : (status_code == 403) ? "Forbidden" : "Unknown";
		string response = "HTTP/1.1 " + to_string(status_code) + " " + reason + "\r\n"
			"Content-Type: " + content_Type + "\r\n"
			"Content-Length: " + to_string(content.size()) + "\r\n"
			"Connection: close\r\n"
			"\r\n" +
			content;
		return response;
	}
};
class StaticFileServer {

public:
	StaticFileServer() {
		root = fs::weakly_canonical("www");
	}

	Response server(const Request& req) {

		string url_path = req.path;
		url_path = url_path.substr(0, req.path.find('?'));

		if (url_path.empty() || url_path == "/") { url_path = "/index.html"; }

		if (!url_path.empty() && url_path.front() == '/') { url_path.erase(0, 1); }

		if (!isPathSafe(url_path)) {
			std::println("Forbidden request path");
			std::println("error here");
			return Response("403 forbidden", 403, "text/plain");
		}

		fs::path full_path = (root / url_path);


		if (!fs::is_regular_file(full_path)) {
			std::println("File not found {}", full_path.string());
			return Response("404 not found ", 404, "text/plain");
		}


		string content = readFile(full_path);
		string content_type = getContentType(full_path);

		return Response(content, 200, content_type);

	}



	bool isPathSafe(const fs::path& requested_path) {

		if (requested_path.is_absolute()) {
			return  false;
		}


		for (const auto& part : requested_path) {
			if (part == "..") {
				return false;
			}
		}
		try {
			fs::path target = fs::weakly_canonical(root / requested_path);

			fs::path relative = target.lexically_relative(root);

			if (relative.empty() || relative.is_absolute()) {
				return false;
			}

			if (*relative.begin() == "..") {
				return false;
			}
			return true;

		}
		catch (const fs::filesystem_error& e) {
			return false;
		}

	}

	string readFile(const fs::path& file_path) {

		ifstream file(file_path, ios::binary);

		if (!file.is_open()) {
			return "";
		}

		string content{
			istreambuf_iterator<char>(file),
			istreambuf_iterator<char>()
		};

		return content;
	}


	string getContentType(const fs::path& path) {

		static const unordered_map<string, string> contentTypes = {
			{ ".html", "text/html" },
			{ ".htm", "text/html" },
			{ ".css", "text/css" },
			{ ".js", "application/javascript" },
			{ ".json", "application/json" },
			{ ".png", "image/png" },
			{ ".jpg", "image/jpeg" },
			{ ".jpeg", "image/jpeg" },
			{ ".gif", "image/gif" },
			{ ".svg", "image/svg+xml" },
			{ ".ico", "image/x-icon" },
			{ ".txt", "text/plain" }
		};


		string ext = path.extension().string();
		auto it = contentTypes.find(ext);
		if (it != contentTypes.end()) {
			return it->second;
		}

		return "application/octet-stream";
	}

private:
	fs::path root;






};
class Router {
public:
	void add_route(const std::string& method, const std::string& path, std::function<Response(const Request&)> handler) {
		routes[method + " " + path] = handler;
	}

	auto dispatch_req(
		const Request& req,
		StaticFileServer& fileserver
	) {

		auto it = routes.find(req.method + " " + req.path);


		if (it != routes.end()) {
			std::println("Route found {} {}", req.method, req.path);
			return it->second(req);

		}
		return fileserver.server(req);
	}

	~Router() {
		routes.clear();
	}
private:
	std::unordered_map <std::string, std::function<Response(const Request&)>> routes;

};


void handle_client(SOCKET clientSoc, StaticFileServer& fileserver , Router& router) {

	try{

		std::println("Client Connected ");

		string req;
		unordered_map<string, string > headers;
		char buffer[1024]{ 0 };
		string method, path, http_version;

		while (true) {
			int n = recv(clientSoc, buffer, sizeof(buffer), 0);
			if (n <= 0) { break; }
			req.append(buffer, n);
			if (req.find("\r\n\r\n") != std::string::npos) { break; }


		}
		std::istringstream iss(req);
		string first_line;
		getline(iss, first_line);
		std::istringstream req_spliter(first_line);
		req_spliter >> method >> path >> http_version;

		while (true) {
			string header, value;
			getline(iss, first_line);
			if (first_line == "\r" || first_line == "") { break; }
			auto pos = first_line.find_first_of(':');
			if (pos == string::npos) { continue; }
			header = first_line.substr(0, pos);
			value = first_line.substr(pos + 1, first_line.length() - pos - 1);

			std::transform(header.begin(), header.end(), header.begin(), [](unsigned char  c) { return std::tolower(c); });

			headers.insert({ header, value });
			std::println("Appended Header: {}, Value: {}", header, value);


		}



		std::println("Request received: {}", first_line);
		std::println("Method: {}, Path: {}, HTTP Version: {}", method, path, http_version);

		Request U_req{ method, path, http_version };
		Response U_resp = router.dispatch_req(U_req, fileserver);
		string string_response = U_resp.send_response();


		int bytes_sent = send(
			clientSoc,
			string_response.c_str(),
			static_cast<int>(string_response.length()),
			0);

		if (bytes_sent != SOCKET_ERROR) {
			std::println("Sent response of {} bytes", bytes_sent);
		}
		else {
			std::println("send failed with error: {}", WSAGetLastError());
		}
	
	
	}
	catch (const exception& e) {
		std::println("Exception occurred while handling client: {}", e.what());

	}
	closesocket(clientSoc);

}





int main() {
	Router router;
	StaticFileServer fileserver;

	router.add_route("GET", "/home", [](const Request& req) {return Response{ " Hello, world!\n From 0x5900FX\n", 200, "text/plain" };});
	router.add_route("GET", "/api/status", [](const Request& req) {return Response{ "status:ok" , 200 ,"application/json" };});


	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(6666);


	Wsocket soc;
	SOCKET S = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (S == INVALID_SOCKET) {
		throw runtime_error("socket failed with error: " + to_string(WSAGetLastError()));

	}
	else {
		std::println("socket created successfully");
	}


	int opt = 1;
	setsockopt(S, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));


	if (::bind(
		S,
		(sockaddr*)&serverAddr,
		sizeof(serverAddr))
		== SOCKET_ERROR)
	{
		throw runtime_error("bind failed with error: " + to_string(WSAGetLastError()));
	}
	else
	{
		std::println("BIND successfully");
	}


	if (listen(S, SOMAXCONN) == SOCKET_ERROR) {
		throw runtime_error("listen failed with error: " + to_string(WSAGetLastError()));

	}


	std::println("Listening on port 6666...");






	while (true) {

		std::println("Waiting for connections");


		SOCKET clientSoc = accept(S, nullptr, nullptr);
		if (clientSoc == INVALID_SOCKET) {
			std::println("Accept failed with error: {}", WSAGetLastError());
			continue;
		}

		
		thread(handle_client, clientSoc, std::ref(fileserver), std::ref(router)).detach();
	}
	closesocket(S);

	return 0;
}
