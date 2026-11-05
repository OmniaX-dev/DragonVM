// dsls: the DScript language server (LSP over stdin / stdout).
//   Diagnostics (as you type), go to definition, hover, document and workspace symbols.
//
// Every analysis runs the compiler (preprocessor, parser and checking, no code generation) in
// a forked child process, which writes its results as JSON to a pipe and exits: the compiler
// never frees its memory, and a crash or an endless loop in it cannot take the server down.
//
// Which file is compiled: a file with a main sub is a root; any other file is analyzed as part
// of a root that includes it (the closest one in the directory tree), or on its own.
//
// Configuration: dsls.json in the workspace root (optional):
//   {
//     "args": ["-I", "extra/ds/sdk"],                      dsc options for every root
//     "files": { "extra/ds/bios/entry.ds": ["--rom"] }     more options for one root
//   }
// Options: -I <dir>, -D NAME[=value], --rom, --raw. Paths are relative to the workspace root.

#include "../compiler/Preprocessor.hpp"
#include "../compiler/Parser.hpp"
#include "../compiler/CodeGen.hpp"
#include <ostd/vendor/nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
	const int AnalysisTimeoutMs = 10000;

	// ======================================= Paths =======================================
	std::string canonical(const std::string& path)
	{
		std::error_code ec;
		fs::path p = fs::weakly_canonical(path, ec);
		return ec ? path : p.string();
	}

	std::string uriToPath(const std::string& uri)
	{
		std::string s = uri;
		if (s.rfind("file://", 0) == 0)
			s = s.substr(7);
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '%' && i + 2 < s.size())
			{
				out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
				i += 2;
			}
			else
				out += s[i];
		}
		return canonical(out);
	}

	std::string pathToUri(const std::string& path)
	{
		std::string out = "file://";
		for (unsigned char c : path)
		{
			if (std::isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-' || c == '~')
				out += (char)c;
			else
			{
				char buf[4];
				std::snprintf(buf, sizeof(buf), "%%%02X", c);
				out += buf;
			}
		}
		return out;
	}

	bool readFile(const std::string& path, std::string& out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}

	bool hasMain(const std::string& text)
	{
		static const std::regex re(R"((^|[^A-Za-z0-9_])sub\s+main\s*\()");
		return std::regex_search(text, re);
	}

	// ====================================== Options ======================================
	struct Options
	{
		std::vector<std::string> includeDirs;
		std::vector<std::pair<std::string, std::string>> defines;
		bool rom { false };
		bool raw { false };
	};

	void applyArgs(Options& o, const std::vector<std::string>& args, const std::string& base)
	{
		for (size_t i = 0; i < args.size(); i++)
		{
			const std::string& a = args[i];
			if ((a == "-I" || a == "-D") && i + 1 >= args.size())
			{
				std::cerr << "dsls: dsls.json: missing value for " << a << "\n";
				break;
			}
			if (a == "-I")
			{
				fs::path p(args[++i]);
				o.includeDirs.push_back(canonical((p.is_absolute() ? p : fs::path(base) / p).string()));
			}
			else if (a == "-D")
			{
				const std::string d = args[++i];
				const size_t eq = d.find('=');
				o.defines.push_back(eq == std::string::npos ? std::make_pair(d, std::string()) : std::make_pair(d.substr(0, eq), d.substr(eq + 1)));
			}
			else if (a == "--rom")
				o.rom = o.raw = true;
			else if (a == "--raw")
				o.raw = true;
			else
				std::cerr << "dsls: dsls.json: unknown option ignored: " << a << "\n";
		}
	}

	// ================================ Analysis (the child) ===============================
	std::vector<std::pair<dscript::SourcePos, std::string>> s_warnings;

	void onWarning(const dscript::SourcePos& pos, const std::string& message)
	{
		s_warnings.push_back({ pos, message });
	}

	// LSP symbol kinds
	enum { KindNamespace = 3, KindMethod = 6, KindField = 8, KindEnum = 10, KindFunction = 12, KindVariable = 13, KindConstant = 14, KindEnumMember = 22, KindStruct = 23, KindTypeParameter = 26 };

	json analyzeInProcess(const std::string& root, const Options& o, const std::map<std::string, std::string>& docs)
	{
		using namespace dscript;
		json out = { { "files", json::array() }, { "diagnostics", json::array() }, { "entries", json::array() }, { "symbols", json::array() } };
		std::map<std::string, std::string> canon;
		auto l_canon = [&](const std::string& f) -> std::string {
			if (f.empty() || f[0] == '<')
				return f;
			auto it = canon.find(f);
			if (it != canon.end())
				return it->second;
			return canon[f] = canonical(f);
		};
		auto l_diag = [&](const SourcePos& p, const std::string& message, int severity) {
			const bool inFile = !p.file.empty() && p.file[0] != '<';
			out["diagnostics"].push_back({ { "file", inFile ? l_canon(p.file) : root }, { "line", inFile ? std::max(p.line, 1) : 1 },
				{ "column", inFile ? std::max(p.column, 1) : 1 }, { "message", message }, { "severity", severity } });
		};

		Index index;
		setWarningHandler(onWarning);
		Preprocessor::setOverlay(&docs);
		Preprocessor pp(o.includeDirs);
		pp.define("__DSCRIPT__", "1");
		if (o.raw)
			pp.define("__RAW__", "1");
		if (o.rom)
			pp.define("__ROM__", "1");
		for (const auto& d : o.defines)
			pp.define(d.first, d.second);
		pp.setIndex(&index);
		Node* program = nullptr;
		try
		{
			std::vector<Token> tokens = pp.processFile(root);
			Parser parser(tokens);
			program = parser.parseProgram();
		}
		catch (const CompileError& e)
		{
			l_diag(e.pos, e.message, 1);
		}
		for (const auto& f : pp.files())
			out["files"].push_back(f);
		if (program != nullptr)
		{
			CodeGenOptions cg;
			cg.raw = o.raw;
			cg.rom = o.rom;
			cg.analysis = true;
			cg.index = &index;
			CodeGen gen(cg);
			try
			{
				gen.generate(program);
			}
			catch (const CompileError& e)
			{
				l_diag(e.pos, e.message, 1);
			}
			for (const auto& e : gen.errors())
				l_diag(e.pos, e.message, 1);

			// Symbols: the top-level declarations (not the hidden ones), fields, enum members.
			auto l_symbol = [&](const std::string& name, int kind, const SourcePos& pos, const std::string& container) {
				if (pos.file.empty() || pos.line <= 0)
					return;
				out["symbols"].push_back({ name, kind, l_canon(pos.file), pos.line, pos.column, (int)name.size(), container });
			};
			for (const Node* d : program->list)
			{
				// Declared names (shortName) in their namespace / struct (scope).
				const std::string& name = d->shortName.empty() ? d->name : d->shortName;
				const std::string container = (d->owner != nullptr && d->kind != NodeKind::FuncDecl) ? d->owner->name : d->scope;
				if (d->kind == NodeKind::FuncDecl && (d->value & 4) != 0)
				{
					l_symbol(name, KindMethod, d->namePos, d->owner != nullptr ? d->owner->name : d->scope);
					continue;
				}
				// Hidden: local constants / statics, lambdas.
				if (!d->text.empty() || d->name.rfind("__lambda_", 0) == 0)
					continue;
				switch (d->kind)
				{
					case NodeKind::NamespaceDecl: l_symbol(name, KindNamespace, d->namePos, container); break;
					case NodeKind::FuncDecl: l_symbol(name, KindFunction, d->namePos, container); break;
					case NodeKind::ConstDecl: l_symbol(name, KindConstant, d->namePos, container); break;
					case NodeKind::VarDecl: l_symbol(name, KindVariable, d->namePos, container); break;
					case NodeKind::AliasDecl: l_symbol(name, KindTypeParameter, d->namePos, container); break;
					case NodeKind::StructDecl:
					{
						const bool anonymous = d->value != 0;
						if (!anonymous)
							l_symbol(name, KindStruct, d->namePos, container);
						for (const Node* f : d->list)
							l_symbol(f->name, KindField, f->namePos, anonymous ? "struct { ... }" : d->name);
					}
					break;
					case NodeKind::EnumDecl:
						l_symbol(name, KindEnum, d->namePos, container);
						for (const Node* m : d->list)
							l_symbol(m->name, KindEnumMember, m->namePos, d->name);
					break;
					default: break;
				}
			}
		}
		for (const auto& w : s_warnings)
			l_diag(w.first, w.second, 2);
		for (const auto& e : index.entries())
		{
			const IndexEntry& x = e.second;
			out["entries"].push_back({ l_canon(x.pos.file), x.pos.line, x.pos.column, x.length, l_canon(x.def.file), x.def.line, x.def.column, x.hover });
		}
		return out;
	}

	// ================================ Analysis (the server) ==============================
	struct Diagnostic
	{
		std::string file;
		int line { 1 };
		int column { 1 };
		std::string message;
		int severity { 1 };
	};

	struct Symbol
	{
		std::string name;
		int kind { 0 };
		std::string file;
		int line { 0 };
		int column { 0 };
		int length { 0 };
		std::string container;
	};

	struct Analysis
	{
		bool valid { false };	// analyzed at least once
		bool dirty { true };	// a file of it changed since
		std::set<std::string> files;
		std::vector<Diagnostic> diagnostics;
		dscript::Index index;
		std::vector<Symbol> symbols;
	};

	class Server
	{
		public:
			int run(void);

		private:
			// ===== protocol =====
			bool inputReady(int timeoutMs);
			bool readMessage(json& msg);
			void send(const json& msg);
			void respond(const json& id, const json& result);
			void respondError(const json& id, int code, const std::string& message);
			void notify(const std::string& method, const json& params);
			void handle(const json& msg);

			// ===== workspace =====
			void initialize(const json& params);
			void loadConfig(void);
			void scanWorkspace(void);
			Options optionsFor(const std::string& root);
			std::string chooseRoot(const std::string& file);
			void markDirty(const std::string& file);
			void documentChanged(const std::string& file);
			Analysis& analyze(const std::string& root);
			Analysis& fresh(const std::string& file);
			void flushPending(void);
			void publish(const std::string& root);

			// ===== positions =====
			std::string lineText(const std::string& file, int line);
			int toColumn(const std::string& file, int line, int character);	// LSP (0-based, UTF-16) -> 1-based byte column
			int toCharacter(const std::string& file, int line, int column);		// the other way
			json range(const std::string& file, int line, int column, int length);

			// ===== requests =====
			json definition(const json& params);
			json hover(const json& params);
			json documentSymbols(const json& params);
			json workspaceSymbols(const json& params);
			json symbolInformation(const Symbol& s);

		private:
			std::string m_buffer;
			std::string m_rootDir;
			Options m_baseOptions;
			std::map<std::string, std::vector<std::string>> m_fileArgs;	// canonical root -> options
			std::map<std::string, std::string> m_docs;					// open documents: canonical path -> text
			std::set<std::string> m_roots;
			std::map<std::string, Analysis> m_analyses;
			std::set<std::string> m_pending;							// roots to analyze and publish when idle
			bool m_shutdown { false };
	};

	// ===================================== Protocol ======================================
	bool Server::inputReady(int timeoutMs)
	{
		if (!m_buffer.empty())
			return true;
		pollfd p { 0, POLLIN, 0 };
		return poll(&p, 1, timeoutMs) > 0;
	}

	bool Server::readMessage(json& msg)
	{
		auto l_fill = [&]() -> bool {
			char buf[65536];
			ssize_t n = read(0, buf, sizeof(buf));
			if (n <= 0)
				return false;
			m_buffer.append(buf, (size_t)n);
			return true;
		};
		size_t headerEnd;
		while ((headerEnd = m_buffer.find("\r\n\r\n")) == std::string::npos)
		{
			if (!l_fill())
				return false;
		}
		size_t length = 0;
		std::istringstream headers(m_buffer.substr(0, headerEnd));
		std::string line;
		while (std::getline(headers, line))
		{
			if (line.rfind("Content-Length:", 0) == 0)
				length = (size_t)std::stoul(line.substr(15));
		}
		const size_t bodyStart = headerEnd + 4;
		while (m_buffer.size() < bodyStart + length)
		{
			if (!l_fill())
				return false;
		}
		const std::string body = m_buffer.substr(bodyStart, length);
		m_buffer.erase(0, bodyStart + length);
		msg = json::parse(body, nullptr, false);
		return true;
	}

	void Server::send(const json& msg)
	{
		const std::string body = msg.dump(-1, ' ', false, json::error_handler_t::replace);
		const std::string data = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
		size_t done = 0;
		while (done < data.size())
		{
			ssize_t n = write(1, data.data() + done, data.size() - done);
			if (n <= 0)
				return;
			done += (size_t)n;
		}
	}

	void Server::respond(const json& id, const json& result)
	{
		send({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
	}

	void Server::respondError(const json& id, int code, const std::string& message)
	{
		send({ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } });
	}

	void Server::notify(const std::string& method, const json& params)
	{
		send({ { "jsonrpc", "2.0" }, { "method", method }, { "params", params } });
	}

	int Server::run(void)
	{
		while (true)
		{
			// Analyses wait until the editor has nothing more to say (typing sends a change per key).
			if (!m_pending.empty() && !inputReady(0))
			{
				flushPending();
				continue;
			}
			json msg;
			if (!readMessage(msg))
				return m_shutdown ? 0 : 1;
			if (msg.is_discarded() || !msg.is_object())
				continue;
			if (msg.value("method", "") == "exit")
				return m_shutdown ? 0 : 1;
			try
			{
				handle(msg);
			}
			catch (const std::exception& e)
			{
				std::cerr << "dsls: " << e.what() << "\n";
				if (msg.contains("id"))
					respondError(msg["id"], -32603, e.what());
			}
		}
	}

	void Server::handle(const json& msg)
	{
		const std::string method = msg.value("method", "");
		const json params = msg.value("params", json::object());
		const bool request = msg.contains("id");
		if (method == "initialize")
		{
			initialize(params);
			respond(msg["id"], {
				{ "capabilities", {
					{ "textDocumentSync", { { "openClose", true }, { "change", 1 }, { "save", { { "includeText", false } } } } },
					{ "definitionProvider", true },
					{ "hoverProvider", true },
					{ "documentSymbolProvider", true },
					{ "workspaceSymbolProvider", true } } },
				{ "serverInfo", { { "name", "dsls" }, { "version", "0.1" } } } });
		}
		else if (method == "initialized")
		{
			// Every root once, to know which files each one includes.
			for (const auto& root : m_roots)
				analyze(root);
		}
		else if (method == "shutdown")
		{
			m_shutdown = true;
			respond(msg["id"], nullptr);
		}
		else if (method == "textDocument/didOpen")
		{
			const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
			m_docs[file] = params.at("textDocument").value("text", "");
			documentChanged(file);
		}
		else if (method == "textDocument/didChange")
		{
			const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
			const json changes = params.value("contentChanges", json::array());
			if (changes.is_array() && !changes.empty())
				m_docs[file] = changes.back().value("text", "");
			documentChanged(file);
		}
		else if (method == "textDocument/didClose")
		{
			const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
			m_docs.erase(file);
			documentChanged(file);	// the file on the disk is used again
		}
		else if (method == "textDocument/definition")
			respond(msg["id"], definition(params));
		else if (method == "textDocument/hover")
			respond(msg["id"], hover(params));
		else if (method == "textDocument/documentSymbol")
			respond(msg["id"], documentSymbols(params));
		else if (method == "workspace/symbol")
			respond(msg["id"], workspaceSymbols(params));
		else if (request)
			respondError(msg["id"], -32601, "method not supported: " + method);
	}

	// ===================================== Workspace =====================================
	void Server::initialize(const json& params)
	{
		std::string uri;
		if (params.contains("rootUri") && params["rootUri"].is_string())
			uri = params["rootUri"].get<std::string>();
		else if (params.contains("workspaceFolders") && params["workspaceFolders"].is_array() && !params["workspaceFolders"].empty())
			uri = params["workspaceFolders"][0].value("uri", "");
		m_rootDir = uri.empty() ? fs::current_path().string() : uriToPath(uri);
		loadConfig();
		scanWorkspace();
		std::cerr << "dsls: workspace " << m_rootDir << ", " << m_roots.size() << " programs (files with main)\n";
	}

	void Server::loadConfig(void)
	{
		std::string text;
		if (!readFile((fs::path(m_rootDir) / "dsls.json").string(), text))
			return;
		json cfg = json::parse(text, nullptr, false, true);
		if (cfg.is_discarded() || !cfg.is_object())
		{
			std::cerr << "dsls: dsls.json is not valid JSON\n";
			return;
		}
		if (cfg.contains("args") && cfg["args"].is_array())
			applyArgs(m_baseOptions, cfg["args"].get<std::vector<std::string>>(), m_rootDir);
		if (cfg.contains("files") && cfg["files"].is_object())
		{
			for (auto& f : cfg["files"].items())
			{
				if (!f.value().is_array())
					continue;
				fs::path p(f.key());
				m_fileArgs[canonical((p.is_absolute() ? p : fs::path(m_rootDir) / p).string())] = f.value().get<std::vector<std::string>>();
			}
		}
	}

	void Server::scanWorkspace(void)
	{
		std::error_code ec;
		fs::recursive_directory_iterator it(m_rootDir, fs::directory_options::skip_permission_denied, ec), end;
		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;
			const fs::path& p = it->path();
			if (it->is_directory(ec))
			{
				// Hidden directories, node_modules, and other repositories inside this one.
				const std::string name = p.filename().string();
				if ((!name.empty() && name[0] == '.') || name == "node_modules" || fs::exists(p / ".git", ec))
					it.disable_recursion_pending();
				continue;
			}
			if (p.extension() != ".ds")
				continue;
			std::string text;
			if (readFile(p.string(), text) && hasMain(text))
				m_roots.insert(canonical(p.string()));
		}
	}

	Options Server::optionsFor(const std::string& root)
	{
		Options o = m_baseOptions;
		auto it = m_fileArgs.find(root);
		if (it != m_fileArgs.end())
			applyArgs(o, it->second, m_rootDir);
		return o;
	}

	// The root a file is analyzed in: itself if it has main, else the closest root that
	// includes it (most leading directories in common), else itself.
	std::string Server::chooseRoot(const std::string& file)
	{
		if (m_roots.count(file) > 0)
			return file;
		const fs::path dir = fs::path(file).parent_path();
		std::string best;
		int bestScore = -1;
		for (const auto& entry : m_analyses)
		{
			if (!entry.second.valid || entry.second.files.count(file) == 0 || m_roots.count(entry.first) == 0)
				continue;
			int score = 0;
			const fs::path rootDir = fs::path(entry.first).parent_path();
			for (auto a = dir.begin(), b = rootDir.begin(); a != dir.end() && b != rootDir.end() && *a == *b; ++a, ++b)
				score++;
			if (score > bestScore)
			{
				best = entry.first;
				bestScore = score;
			}
		}
		return best.empty() ? file : best;
	}

	void Server::markDirty(const std::string& file)
	{
		for (auto& entry : m_analyses)
		{
			if (entry.first == file || entry.second.files.count(file) > 0)
				entry.second.dirty = true;
		}
	}

	void Server::documentChanged(const std::string& file)
	{
		std::string text;
		auto doc = m_docs.find(file);
		if (doc != m_docs.end())
			text = doc->second;
		else
			readFile(file, text);
		if (hasMain(text))
			m_roots.insert(file);
		else
			m_roots.erase(file);
		markDirty(file);
		m_pending.insert(chooseRoot(file));
	}

	Analysis& Server::analyze(const std::string& root)
	{
		Analysis& a = m_analyses[root];
		const Options options = optionsFor(root);
		std::string output;
		int fds[2];
		bool ok = false;
		if (pipe(fds) == 0)
		{
			pid_t pid = fork();
			if (pid == 0)
			{
				close(fds[0]);
				// stdout is the editor's channel.
				int devNull = open("/dev/null", O_WRONLY);
				if (devNull >= 0)
					dup2(devNull, 1);
				std::string data;
				try
				{
					data = analyzeInProcess(root, options, m_docs).dump(-1, ' ', false, json::error_handler_t::replace);
				}
				catch (const std::exception& e)
				{
					data = json({ { "error", e.what() } }).dump();
				}
				size_t done = 0;
				while (done < data.size())
				{
					ssize_t n = write(fds[1], data.data() + done, data.size() - done);
					if (n <= 0)
						break;
					done += (size_t)n;
				}
				_exit(0);
			}
			close(fds[1]);
			if (pid > 0)
			{
				bool timedOut = false;
				char buf[65536];
				while (true)
				{
					pollfd p { fds[0], POLLIN, 0 };
					if (poll(&p, 1, AnalysisTimeoutMs) <= 0)
					{
						timedOut = true;
						kill(pid, SIGKILL);
						break;
					}
					ssize_t n = read(fds[0], buf, sizeof(buf));
					if (n <= 0)
						break;
					output.append(buf, (size_t)n);
				}
				int status = 0;
				waitpid(pid, &status, 0);
				ok = !timedOut && WIFEXITED(status) && WEXITSTATUS(status) == 0;
			}
			close(fds[0]);
		}
		json r = ok ? json::parse(output, nullptr, false) : json();
		a.valid = true;
		a.dirty = false;
		a.diagnostics.clear();
		a.symbols.clear();
		a.index = dscript::Index();
		if (!ok || r.is_discarded() || !r.is_object() || r.contains("error"))
		{
			const std::string why = (r.is_object() && r.contains("error")) ? r["error"].get<std::string>() : "the compiler crashed or did not finish";
			a.diagnostics.push_back({ root, 1, 1, "dsls: the analysis failed (" + why + ")", 1 });
			std::cerr << "dsls: analysis of " << root << " failed: " << why << "\n";
			return a;
		}
		a.files.clear();
		for (const auto& f : r["files"])
			a.files.insert(f.get<std::string>());
		a.files.insert(root);
		for (const auto& d : r["diagnostics"])
			a.diagnostics.push_back({ d["file"].get<std::string>(), d["line"].get<int>(), d["column"].get<int>(), d["message"].get<std::string>(), d["severity"].get<int>() });
		for (const auto& e : r["entries"])
		{
			a.index.add({ e[0].get<std::string>(), e[1].get<int>(), e[2].get<int>() }, e[3].get<int>(),
				{ e[4].get<std::string>(), e[5].get<int>(), e[6].get<int>() }, e[7].get<std::string>());
		}
		for (const auto& s : r["symbols"])
		{
			a.symbols.push_back({ s[0].get<std::string>(), s[1].get<int>(), s[2].get<std::string>(), s[3].get<int>(), s[4].get<int>(),
				s[5].get<int>(), s[6].get<std::string>() });
		}
		return a;
	}

	void Server::flushPending(void)
	{
		const auto pending = m_pending;
		m_pending.clear();
		for (const auto& root : pending)
		{
			Analysis& a = m_analyses[root];
			if (!a.valid || a.dirty)
				analyze(root);
			publish(root);
		}
	}

	// The analysis to answer a request about the file with (up to date).
	Analysis& Server::fresh(const std::string& file)
	{
		const std::string root = chooseRoot(file);
		Analysis& a = m_analyses[root];
		if (!a.valid || a.dirty)
		{
			analyze(root);
			publish(root);
			m_pending.erase(root);
		}
		return m_analyses[root];
	}

	// Diagnostics of every file that this root is the one for (an empty list clears them).
	void Server::publish(const std::string& root)
	{
		Analysis& a = m_analyses[root];
		std::map<std::string, json> perFile;
		for (const auto& f : a.files)
		{
			if (chooseRoot(f) == root)
				perFile[f] = json::array();
		}
		perFile[root];
		for (const auto& d : a.diagnostics)
		{
			auto it = perFile.find(d.file);
			if (it == perFile.end())
				continue;
			if (!it->second.is_array())
				it->second = json::array();
			// The range: the word at the position (at least one character).
			const std::string text = lineText(d.file, d.line);
			size_t end = (size_t)d.column - 1;
			while (end < text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_'))
				end++;
			const int length = std::max(1, (int)end - (d.column - 1));
			it->second.push_back({ { "range", range(d.file, d.line, d.column, length) }, { "severity", d.severity },
				{ "source", "dsc" }, { "message", d.message } });
		}
		for (auto& f : perFile)
		{
			if (!f.second.is_array())
				f.second = json::array();
			notify("textDocument/publishDiagnostics", { { "uri", pathToUri(f.first) }, { "diagnostics", f.second } });
		}
	}

	// ===================================== Positions =====================================
	std::string Server::lineText(const std::string& file, int line)
	{
		std::string text;
		auto doc = m_docs.find(file);
		if (doc != m_docs.end())
			text = doc->second;
		else if (!readFile(file, text))
			return "";
		size_t start = 0;
		for (int i = 1; i < line; i++)
		{
			start = text.find('\n', start);
			if (start == std::string::npos)
				return "";
			start++;
		}
		size_t end = text.find('\n', start);
		std::string s = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!s.empty() && s.back() == '\r')
			s.pop_back();
		return s;
	}

	static int utf8Length(unsigned char c)
	{
		if (c < 0x80) return 1;
		if ((c >> 5) == 6) return 2;
		if ((c >> 4) == 14) return 3;
		if ((c >> 3) == 30) return 4;
		return 1;
	}

	int Server::toColumn(const std::string& file, int line, int character)
	{
		const std::string text = lineText(file, line + 1);
		size_t b = 0;
		int units = 0;
		while (b < text.size() && units < character)
		{
			const int len = utf8Length((unsigned char)text[b]);
			units += (len == 4 ? 2 : 1);
			b += (size_t)len;
		}
		return (int)b + 1 + (character - units);	// past the end of the line: as many columns
	}

	int Server::toCharacter(const std::string& file, int line, int column)
	{
		const std::string text = lineText(file, line);
		size_t b = 0;
		int units = 0;
		while (b < text.size() && (int)b < column - 1)
		{
			const int len = utf8Length((unsigned char)text[b]);
			units += (len == 4 ? 2 : 1);
			b += (size_t)len;
		}
		return units + std::max(0, column - 1 - (int)b);
	}

	json Server::range(const std::string& file, int line, int column, int length)
	{
		const int l = std::max(0, line - 1);
		return { { "start", { { "line", l }, { "character", toCharacter(file, line, column) } } },
				 { "end", { { "line", l }, { "character", toCharacter(file, line, column + length) } } } };
	}

	// ===================================== Requests ======================================
	json Server::definition(const json& params)
	{
		const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
		const int line = params.at("position").at("line").get<int>(), character = params.at("position").at("character").get<int>();
		Analysis& a = fresh(file);
		const dscript::IndexEntry* e = a.index.at(file, line + 1, toColumn(file, line, character));
		if (e == nullptr || e->def.file.empty())
			return nullptr;
		const dscript::IndexEntry* decl = a.index.declaration(e->def);
		const int length = decl != nullptr ? decl->length : 0;
		return { { "uri", pathToUri(e->def.file) }, { "range", range(e->def.file, e->def.line, e->def.column, length) } };
	}

	json Server::hover(const json& params)
	{
		const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
		const int line = params.at("position").at("line").get<int>(), character = params.at("position").at("character").get<int>();
		Analysis& a = fresh(file);
		const dscript::IndexEntry* e = a.index.at(file, line + 1, toColumn(file, line, character));
		if (e == nullptr || e->hover.empty())
			return nullptr;
		std::string where;
		if (!e->def.file.empty() && e->def.file != file)
		{
			std::error_code ec;
			fs::path rel = fs::relative(e->def.file, m_rootDir, ec);
			where = "\n\n" + (ec || rel.empty() ? e->def.file : rel.string()) + ":" + std::to_string(e->def.line);
		}
		return { { "contents", { { "kind", "markdown" }, { "value", "```dscript\n" + e->hover + "\n```" + where } } },
				 { "range", range(file, e->pos.line, e->pos.column, e->length) } };
	}

	json Server::symbolInformation(const Symbol& s)
	{
		json info = { { "name", s.name }, { "kind", s.kind },
					  { "location", { { "uri", pathToUri(s.file) }, { "range", range(s.file, s.line, s.column, s.length) } } } };
		if (!s.container.empty())
			info["containerName"] = s.container;
		return info;
	}

	json Server::documentSymbols(const json& params)
	{
		const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
		Analysis& a = fresh(file);
		json out = json::array();
		for (const Symbol& s : a.symbols)
		{
			if (s.file == file)
				out.push_back(symbolInformation(s));
		}
		return out;
	}

	json Server::workspaceSymbols(const json& params)
	{
		flushPending();	// up to date with every change so far
		std::string query = params.value("query", "");
		std::transform(query.begin(), query.end(), query.begin(), ::tolower);
		json out = json::array();
		std::set<std::tuple<std::string, int, int>> seen;
		for (const auto& entry : m_analyses)
		{
			for (const Symbol& s : entry.second.symbols)
			{
				std::string name = s.name;
				std::transform(name.begin(), name.end(), name.begin(), ::tolower);
				if (name.find(query) == std::string::npos || !seen.insert({ s.file, s.line, s.column }).second)
					continue;
				out.push_back(symbolInformation(s));
				if (out.size() >= 500)
					return out;
			}
		}
		return out;
	}
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		const std::string a = argv[i];
		if (a == "--help" || a == "-h")
		{
			std::cout << "dsls - the DScript language server (LSP on stdin / stdout), started by the editor.\n"
						 "Configuration: dsls.json in the workspace root (see src/lsp/dsls_main.cpp).\n";
			return 0;
		}
		if (a == "--version")
		{
			std::cout << "dsls 0.1\n";
			return 0;
		}
	}
	signal(SIGPIPE, SIG_IGN);
	Server server;
	return server.run();
}
