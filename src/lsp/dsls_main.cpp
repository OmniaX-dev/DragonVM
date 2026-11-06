// dsls: the DScript language server (LSP over stdin / stdout).
//   Diagnostics (as you type), go to definition, hover, completion, document and workspace symbols.
//
// Completion: the compiler describes what the program declares (CodeGen::indexCompletion: the
// globals, the members of structs / enums, every local with where its scope ends, the subs'
// bodies). The server reads the expression before the cursor (a.b->c()[i]., Ns::, ...) and
// works out its type from that. The line being typed rarely parses: the program is analyzed
// again without it (its braces kept), else the last description that parsed is used.
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
// Options: -I <dir>, -L <dir>, -D NAME[=value], --rom, --raw. Paths are relative to the workspace root.

#include "../compiler/Preprocessor.hpp"
#include "../compiler/Parser.hpp"
#include "../compiler/CodeGen.hpp"
#include <ostd/vendor/nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <memory>
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
		std::vector<std::string> libraryDirs;
		std::vector<std::pair<std::string, std::string>> defines;
		bool rom { false };
		bool raw { false };
	};

	void applyArgs(Options& o, const std::vector<std::string>& args, const std::string& base)
	{
		for (size_t i = 0; i < args.size(); i++)
		{
			const std::string& a = args[i];
			if ((a == "-I" || a == "-L" || a == "-D") && i + 1 >= args.size())
			{
				std::cerr << "dsls: dsls.json: missing value for " << a << "\n";
				break;
			}
			if (a == "-I")
			{
				fs::path p(args[++i]);
				o.includeDirs.push_back(canonical((p.is_absolute() ? p : fs::path(base) / p).string()));
			}
			else if (a == "-L")
			{
				fs::path p(args[++i]);
				o.libraryDirs.push_back(canonical((p.is_absolute() ? p : fs::path(base) / p).string()));
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
		pp.setLibraryDirs(o.libraryDirs);
		pp.setIndex(&index);
		pp.define("__DSCRIPT__", "1");
		if (o.raw)
			pp.define("__RAW__", "1");
		if (o.rom)
			pp.define("__ROM__", "1");
		for (const auto& d : o.defines)
			pp.define(d.first, d.second);
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
		// Completion: what the program declares (only when it could be parsed).
		out["parsed"] = program != nullptr;
		if (program != nullptr)
		{
			const Completion& c = index.completion;
			auto l_pos = [&](const SourcePos& p) { return json::array({ l_canon(p.file), p.line, p.column }); };
			auto l_symbols = [&](const std::vector<CompletionSymbol>& list) {
				json a = json::array();
				for (const CompletionSymbol& s : list)
					a.push_back({ s.name, s.parent, s.kind, s.type, s.hover, l_pos(s.def), l_pos(s.end), s.isPrivate });
				return a;
			};
			auto l_contexts = [&](const std::vector<CompletionContext>& list) {
				json a = json::array();
				for (const CompletionContext& x : list)
					a.push_back({ l_pos(x.start), l_pos(x.end), x.scope, x.structs });
				return a;
			};
			out["completion"] = { { "globals", l_symbols(c.globals) }, { "members", l_symbols(c.members) }, { "locals", l_symbols(c.locals) },
				{ "subs", l_contexts(c.subs) }, { "namespaces", l_contexts(c.namespaces) } };
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

	// What completion knows about a program (see Completion in Index.hpp).
	struct CompletionModel
	{
		bool valid { false };
		dscript::Completion data;
		std::map<std::string, std::vector<const dscript::CompletionSymbol*>> byFullName;	// globals
		std::map<std::string, std::vector<const dscript::CompletionSymbol*>> byParent;		// globals
		std::map<std::string, std::vector<const dscript::CompletionSymbol*>> membersOf;	// struct / enum -> members

		void load(const json& j);
	};

	struct Analysis
	{
		bool valid { false };	// analyzed at least once
		bool dirty { true };	// a file of it changed since
		std::set<std::string> files;
		std::vector<Diagnostic> diagnostics;
		dscript::Index index;
		std::vector<Symbol> symbols;
		bool parsed { false };					// the last analysis could parse the program
		std::shared_ptr<CompletionModel> completion;	// of the last analysis that could
	};

	void CompletionModel::load(const json& j)
	{
		using namespace dscript;
		auto l_pos = [](const json& p) { return SourcePos { p[0].get<std::string>(), p[1].get<int>(), p[2].get<int>() }; };
		auto l_symbols = [&](const json& a, std::vector<CompletionSymbol>& out) {
			for (const auto& s : a)
			{
				out.push_back({ s[0].get<std::string>(), s[1].get<std::string>(), s[2].get<int>(), s[3].get<std::string>(),
					s[4].get<std::string>(), l_pos(s[5]), l_pos(s[6]), s[7].get<bool>() });
			}
		};
		auto l_contexts = [&](const json& a, std::vector<CompletionContext>& out) {
			for (const auto& x : a)
				out.push_back({ l_pos(x[0]), l_pos(x[1]), x[2].get<std::string>(), x[3].get<std::vector<std::string>>() });
		};
		l_symbols(j["globals"], data.globals);
		l_symbols(j["members"], data.members);
		l_symbols(j["locals"], data.locals);
		l_contexts(j["subs"], data.subs);
		l_contexts(j["namespaces"], data.namespaces);
		for (const CompletionSymbol& s : data.globals)
		{
			byFullName[s.parent.empty() ? s.name : s.parent + "::" + s.name].push_back(&s);
			byParent[s.parent].push_back(&s);
		}
		for (const CompletionSymbol& s : data.members)
			membersOf[s.parent].push_back(&s);
		valid = true;
	}

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
			// Runs the analysis of root (with these open documents) in a child process.
			bool runAnalysis(const std::string& root, const std::map<std::string, std::string>& docs, json& result, std::string& why);
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
			json completion(const json& params);
			// The program as it would be without the line being typed (it rarely parses).
			std::shared_ptr<CompletionModel> completionWithoutLine(const std::string& root, const std::string& file, int line);
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
			std::string m_lineModelKey;									// completionWithoutLine(): the last one
			std::shared_ptr<CompletionModel> m_lineModel;
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
					{ "workspaceSymbolProvider", true },
					{ "completionProvider", { { "triggerCharacters", { ".", ":", ">" } }, { "resolveProvider", false } } } } },
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
		else if (method == "textDocument/completion")
			respond(msg["id"], completion(params));
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

	bool Server::runAnalysis(const std::string& root, const std::map<std::string, std::string>& docs, json& r, std::string& why)
	{
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
					data = analyzeInProcess(root, options, docs).dump(-1, ' ', false, json::error_handler_t::replace);
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
		r = ok ? json::parse(output, nullptr, false) : json();
		if (!ok || r.is_discarded() || !r.is_object() || r.contains("error"))
		{
			why = (r.is_object() && r.contains("error")) ? r["error"].get<std::string>() : "the compiler crashed or did not finish";
			return false;
		}
		return true;
	}

	Analysis& Server::analyze(const std::string& root)
	{
		Analysis& a = m_analyses[root];
		json r;
		std::string why;
		const bool ok = runAnalysis(root, m_docs, r, why);
		a.valid = true;
		a.dirty = false;
		a.diagnostics.clear();
		a.symbols.clear();
		a.index = dscript::Index();
		a.parsed = false;
		if (!ok)
		{
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
		a.parsed = r.value("parsed", false) && r.contains("completion");
		if (a.parsed)
		{
			a.completion = std::make_shared<CompletionModel>();
			a.completion->load(r["completion"]);
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

	// ===================================== Completion ====================================
	bool isIdentChar(char c)
	{
		return std::isalnum((unsigned char)c) || c == '_';
	}

	std::string rtrim(std::string s)
	{
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
			s.pop_back();
		return s;
	}

	// The cursor (line: 1-based, column: 0-based byte) is in a comment, a string / character
	// literal or a low block (dasm).
	bool notCode(const std::string& text, int line, size_t column)
	{
		size_t offset = 0;
		for (int l = 1; l < line; l++)
		{
			offset = text.find('\n', offset);
			if (offset == std::string::npos)
				return false;
			offset++;
		}
		offset = std::min(offset + column, text.size());
		enum { Code, LineComment, BlockComment, String, Char, Low } state = Code;
		int depth = 0;
		for (size_t i = 0; i < offset; i++)
		{
			const char c = text[i];
			const char n = i + 1 < text.size() ? text[i + 1] : 0;
			switch (state)
			{
				case Code:
					if (c == '/' && n == '/') { state = LineComment; i++; }
					else if (c == '/' && n == '*') { state = BlockComment; i++; }
					else if (c == '"') state = String;
					else if (c == '\'') state = Char;
					else if (c == 'l' && text.compare(i, 3, "low") == 0 && (i == 0 || !isIdentChar(text[i - 1])))
					{
						size_t j = i + 3;
						while (j < text.size() && std::isspace((unsigned char)text[j]))
							j++;
						if (j < text.size() && text[j] == '{' && j < offset)
						{
							state = Low;
							depth = 1;
							i = j;
						}
					}
				break;
				case LineComment: if (c == '\n') state = Code; break;
				case BlockComment: if (c == '*' && n == '/') { state = Code; i++; } break;
				case String: if (c == '\\') i++; else if (c == '"' || c == '\n') state = Code; break;
				case Char: if (c == '\\') i++; else if (c == '\'' || c == '\n') state = Code; break;
				case Low:
					if (c == '{') depth++;
					else if (c == '}' && --depth == 0) state = Code;
				break;
			}
		}
		return state != Code;
	}

	// A postfix expression, as written before a '.' / '->' / '::': a.b->c(1)[2], Ns::f(), cast<T*>(p).
	struct Segment
	{
		enum Kind { Name, Dot, Arrow, Scope, Call, Index, Cast } kind;
		std::string text;	// Name: the name; Cast: the type
	};

	// The chain that ends at `end` in s, first to last (global: it starts with ::). false: none.
	bool parseChain(const std::string& s, size_t end, std::vector<Segment>& out, bool& global, size_t& chainStart)
	{
		size_t i = end;
		global = false;
		std::vector<Segment> rev;
		auto l_skip = [&]() { while (i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t')) i--; };
		// s[i - 1] is `close`: i becomes the position of its `open`.
		auto l_match = [&](char open, char close) -> bool {
			int depth = 0;
			while (i > 0)
			{
				const char c = s[--i];
				if (c == close)
					depth++;
				else if (c == open && --depth == 0)
					return true;
			}
			return false;
		};
		while (true)
		{
			l_skip();
			if (i == 0)
				break;
			const char c = s[i - 1];
			if (c == ')')
			{
				const size_t close = i;
				if (!l_match('(', ')'))
					return false;
				const size_t open = i;
				l_skip();
				if (i > 0 && s[i - 1] == '>')
				{
					const size_t gt = i;
					if (!l_match('<', '>'))
						return false;
					const std::string type = s.substr(i + 1, gt - i - 2);
					l_skip();
					const size_t e = i;
					while (i > 0 && isIdentChar(s[i - 1]))
						i--;
					if (s.substr(i, e - i) != "cast")
						return false;
					rev.push_back({ Segment::Cast, type });
					break;
				}
				rev.push_back({ Segment::Call, s.substr(open + 1, close - open - 2) });
				continue;
			}
			if (c == ']')
			{
				if (!l_match('[', ']'))
					return false;
				rev.push_back({ Segment::Index, "" });
				continue;
			}
			if (isIdentChar(c))
			{
				const size_t e = i;
				while (i > 0 && isIdentChar(s[i - 1]))
					i--;
				rev.push_back({ Segment::Name, s.substr(i, e - i) });
				l_skip();
				if (i >= 2 && s[i - 1] == ':' && s[i - 2] == ':')
				{
					i -= 2;
					l_skip();
					if (i == 0 || !isIdentChar(s[i - 1]))
					{
						global = true;
						break;
					}
					rev.push_back({ Segment::Scope, "" });
					continue;
				}
				if (i >= 2 && s[i - 1] == '>' && s[i - 2] == '-')
				{
					i -= 2;
					rev.push_back({ Segment::Arrow, "" });
					continue;
				}
				if (i >= 1 && s[i - 1] == '.')
				{
					i -= 1;
					rev.push_back({ Segment::Dot, "" });
					continue;
				}
				break;
			}
			return false;
		}
		out.assign(rev.rbegin(), rev.rend());
		chainStart = i;
		return !out.empty() && (out[0].kind == Segment::Name || out[0].kind == Segment::Cast);
	}

	bool before(const dscript::SourcePos& a, int line, int column)	// a <= (line, column)
	{
		return a.line < line || (a.line == line && a.column <= column);
	}

	bool contains(const dscript::CompletionContext& c, const std::string& file, int line, int column)
	{
		return c.start.file == file && before(c.start, line, column) && !before(c.end, line, column - 1);
	}

	std::shared_ptr<CompletionModel> Server::completionWithoutLine(const std::string& root, const std::string& file, int line)
	{
		std::map<std::string, std::string> docs = m_docs;
		std::string text;
		auto doc = docs.find(file);
		if (doc != docs.end())
			text = doc->second;
		else if (!readFile(file, text))
			return nullptr;
		size_t start = 0;
		for (int i = 1; i < line; i++)
		{
			start = text.find('\n', start);
			if (start == std::string::npos)
				return nullptr;
			start++;
		}
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		// Without the line, but with its braces (if (p.x) { -> {): the blocks stay balanced.
		for (size_t i = start; i < end; i++)
		{
			if (text[i] != '{' && text[i] != '}' && text[i] != '\t' && text[i] != '\r')
				text[i] = ' ';
		}
		docs[file] = text;
		std::string key = root;
		for (const auto& d : docs)
			key += "\n" + std::to_string(d.first.size()) + d.first + std::to_string(d.second.size()) + d.second;
		if (key == m_lineModelKey)
			return m_lineModel;
		m_lineModelKey = key;
		m_lineModel = nullptr;
		json r;
		std::string why;
		if (runAnalysis(root, docs, r, why) && r.value("parsed", false) && r.contains("completion"))
		{
			m_lineModel = std::make_shared<CompletionModel>();
			m_lineModel->load(r["completion"]);
		}
		return m_lineModel;
	}

	json Server::completion(const json& params)
	{
		using namespace dscript;
		typedef CompletionSymbol CS;
		const std::string file = uriToPath(params.at("textDocument").at("uri").get<std::string>());
		const int line = params.at("position").at("line").get<int>(), character = params.at("position").at("character").get<int>();
		const json context = params.value("context", json::object());
		const bool invoked = context.value("triggerKind", 1) == 1;
		json items = json::array();
		const json none = { { "isIncomplete", true }, { "items", items } };

		std::string text;
		auto doc = m_docs.find(file);
		if (doc != m_docs.end())
			text = doc->second;
		else if (!readFile(file, text))
			return none;
		const std::string lineStr = lineText(file, line + 1);
		const size_t cursor = std::min((size_t)std::max(0, toColumn(file, line, character) - 1), lineStr.size());
		if (notCode(text, line + 1, cursor))
			return none;
		const std::string head = lineStr.substr(0, cursor);
		const size_t firstChar = head.find_first_not_of(" \t");
		if (firstChar != std::string::npos && head[firstChar] == '#')
			return none;	// a directive
		size_t start = cursor;
		while (start > 0 && isIdentChar(head[start - 1]))
			start--;
		const std::string prefix = head.substr(start);
		if (!prefix.empty() && std::isdigit((unsigned char)prefix[0]))
			return none;
		const std::string rest = rtrim(head.substr(0, start));

		// What comes before the name being typed.
		enum { General, Dot, Arrow, Scope, TypeName } where = General;
		auto l_endsWith = [&](const std::string& s) { return rest.size() >= s.size() && rest.compare(rest.size() - s.size(), s.size(), s) == 0; };
		static const std::regex returnType(R"(\bsub\b[^;{}=]*\)\s*->$)");
		static const std::regex declType(R"((\b(var|const)\s+[A-Za-z_]\w*|[(,]\s*[A-Za-z_]\w*)\s*:$|\benum\s+[A-Za-z_]\w*\s*:$|\balias\s+[A-Za-z_]\w*\s*=$|\b(cast|sizeof)\s*<$)");
		if (l_endsWith("->"))
			where = std::regex_search(rest, returnType) ? TypeName : Arrow;
		else if (l_endsWith("::"))
			where = Scope;
		else if (l_endsWith(".") && !l_endsWith(".."))
			where = Dot;
		else if (std::regex_search(rest, declType))
			where = TypeName;
		if (where == General && prefix.empty() && !invoked)
			return none;	// ':' / '>' not after a name

		Analysis& a = fresh(file);
		std::shared_ptr<CompletionModel> model = a.parsed ? a.completion : completionWithoutLine(chooseRoot(file), file, line + 1);
		if (!model)
			model = a.completion;	// the last one that parsed
		if (!model)
			return none;
		const Completion& data = model->data;

		// Where the cursor is: the sub (the innermost), or the namespace block.
		const int curLine = line + 1, curColumn = (int)cursor + 1;
		const CompletionContext* sub = nullptr;
		for (const CompletionContext& c : data.subs)
		{
			if (contains(c, file, curLine, curColumn) && (sub == nullptr || before(sub->start, c.start.line, c.start.column)))
				sub = &c;
		}
		std::string scope;
		std::set<std::string> privs;
		if (sub != nullptr)
		{
			scope = sub->scope;
			privs.insert(sub->structs.begin(), sub->structs.end());
		}
		else
		{
			const CompletionContext* ns = nullptr;
			for (const CompletionContext& c : data.namespaces)
			{
				if (contains(c, file, curLine, curColumn) && (ns == nullptr || before(ns->start, c.start.line, c.start.column)))
					ns = &c;
			}
			if (ns != nullptr)
				scope = ns->scope;
		}
		std::vector<std::string> ancestors;
		for (std::string sc = scope; ; )
		{
			ancestors.push_back(sc);
			if (sc.empty())
				break;
			const size_t sep = sc.rfind("::");
			sc = sep == std::string::npos ? "" : sc.substr(0, sep);
		}
		auto l_full = [](const CS* s) { return s->parent.empty() ? s->name : s->parent + "::" + s->name; };
		auto l_kindOf = [&](const std::string& full) -> int {
			auto it = model->byFullName.find(full);
			return (it == model->byFullName.end() || it->second.empty()) ? -1 : it->second[0]->kind;
		};
		// A name as written from the cursor's scope (the innermost first; in a struct, only its
		// nested types), like the compiler.
		auto l_lookup = [&](const std::string& q, bool global) -> const CS* {
			const std::string first = q.substr(0, q.find("::"));
			for (const std::string& anc : ancestors)
			{
				if (global && !anc.empty())
					continue;
				if (l_kindOf(anc) == CS::Struct && l_kindOf(anc + "::" + first) != CS::Struct && l_kindOf(anc + "::" + first) != CS::Enum)
					continue;
				auto it = model->byFullName.find(anc.empty() ? q : anc + "::" + q);
				if (it != model->byFullName.end() && !it->second.empty())
					return it->second[0];
			}
			return nullptr;
		};
		// The locals at the cursor (the innermost declaration of each name).
		std::map<std::string, const CS*> locals;
		for (const CS& l : data.locals)
		{
			if (l.def.file != file || !before(l.def, curLine, curColumn) || !l.end.file.empty() && before(l.end, curLine, curColumn - 1))
				continue;
			if (sub != nullptr && !before(sub->start, l.def.line, l.def.column))
				continue;
			auto it = locals.find(l.name);
			if (it == locals.end() || before(it->second->def, l.def.line, l.def.column))
				locals[l.name] = &l;
		}

		// The value / type a chain is.
		struct Value
		{
			enum Kind { None, Typed, StructType, EnumType, Namespace } kind { None };
			std::string s;		// Typed: its type; the others: the full name
		};
		auto l_symbolValue = [&](const CS* s) -> Value {
			if (s == nullptr)
				return {};
			switch (s->kind)
			{
				case CS::Struct: return { Value::StructType, l_full(s) };
				case CS::Enum: return { Value::EnumType, l_full(s) };
				case CS::Namespace: return { Value::Namespace, l_full(s) };
				case CS::Alias: return s->type.rfind("s:", 0) == 0 ? Value { Value::StructType, s->type.substr(2) } : Value {};
				case CS::Define: return {};
				default: return { Value::Typed, s->type };
			}
		};
		auto l_typeText = [&](std::string t) -> std::string {
			t = rtrim(t.substr(std::min(t.size(), t.find_first_not_of(" \t"))));
			size_t e = 0;
			while (e < t.size() && (isIdentChar(t[e]) || t[e] == ':'))
				e++;
			const std::string name = t.substr(0, e);
			std::string code = "i";
			if (name == "void")
				code = "v";
			else if (name == "string")
				code = "p:i";
			else if (const CS* s = l_lookup(name.rfind("::", 0) == 0 ? name.substr(2) : name, name.rfind("::", 0) == 0))
				code = s->kind == CS::Struct ? "s:" + l_full(s) : s->kind == CS::Alias ? s->type : "i";
			for (size_t i = e; i < t.size(); i++)
			{
				if (t[i] == '*')
					code = "p:" + code;
				else if (t[i] == '[')
					code = "a:" + code;
			}
			return code;
		};
		auto l_member = [&](const std::string& owner, const std::string& name) -> const CS* {
			auto it = model->membersOf.find(owner);
			if (it == model->membersOf.end())
				return nullptr;
			for (const CS* m : it->second)
			{
				if (m->name == name)
					return m;
			}
			return nullptr;
		};
		auto l_evaluate = [&](const std::vector<Segment>& chain, bool global) -> Value {
			Value v;
			size_t k = 1;
			if (chain[0].kind == Segment::Cast)
				v = { Value::Typed, l_typeText(chain[0].text) };
			else
			{
				std::string q = chain[0].text;
				while (k + 1 < chain.size() && chain[k].kind == Segment::Scope && chain[k + 1].kind == Segment::Name)
				{
					q += "::" + chain[k + 1].text;
					k += 2;
				}
				auto local = locals.find(q);
				if (!global && local != locals.end())
					v = { Value::Typed, local->second->type };
				else
					v = l_symbolValue(l_lookup(q, global));
			}
			for (; k < chain.size() && v.kind != Value::None; k++)
			{
				const Segment& seg = chain[k];
				if (seg.kind == Segment::Call)
					v = (v.kind == Value::Typed && v.s.rfind("f:", 0) == 0) ? Value { Value::Typed, v.s.substr(2) } : Value {};
				else if (seg.kind == Segment::Index)
					v = (v.kind == Value::Typed && (v.s.rfind("a:", 0) == 0 || v.s.rfind("p:", 0) == 0)) ? Value { Value::Typed, v.s.substr(2) } : Value {};
				else if (k + 1 < chain.size() && chain[k + 1].kind == Segment::Name)
				{
					const std::string& name = chain[++k].text;
					if (seg.kind == Segment::Scope)
					{
						auto it = model->byFullName.find(v.s + "::" + name);
						v = (v.kind != Value::Typed && it != model->byFullName.end()) ? l_symbolValue(it->second[0]) : Value {};
					}
					else if (seg.kind == Segment::Arrow)
						v = (v.kind == Value::Typed && v.s.rfind("p:s:", 0) == 0) ? l_symbolValue(l_member(v.s.substr(4), name)) : Value {};
					else if (v.kind == Value::Typed && v.s.rfind("s:", 0) == 0)
						v = l_symbolValue(l_member(v.s.substr(2), name));
					else if (v.kind == Value::StructType)
						v = l_symbolValue(l_member(v.s, name));
					else if (v.kind == Value::EnumType)
						v = { Value::Typed, "i" };
					else
						v = {};
				}
				else
					v = {};
			}
			return v;
		};

		// The items: the name being typed is somewhere in them (in order, any case).
		std::map<std::string, size_t> seen;
		std::string lowerPrefix = prefix;
		std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
		const json editRange = { { "start", { { "line", line }, { "character", toCharacter(file, curLine, (int)start + 1) } } },
								 { "end", { { "line", line }, { "character", character } } } };
		auto l_add = [&](const std::string& label, int kind, const std::string& hover, int group) {
			size_t p = 0;
			for (char c : label)
			{
				if (p < lowerPrefix.size() && std::tolower((unsigned char)c) == lowerPrefix[p])
					p++;
			}
			if (p < lowerPrefix.size())
				return;
			auto it = seen.find(label);
			if (it != seen.end())
			{
				// Overloads: every signature in the documentation.
				json& item = items[it->second];
				if (!hover.empty() && item.contains("documentation"))
				{
					std::string doc = item["documentation"]["value"].get<std::string>();
					doc.insert(doc.size() - 4, "\n" + hover);
					item["documentation"]["value"] = doc;
				}
				return;
			}
			static const int lspKinds[] = { 9, 22, 13, 25, 21, 6, 3, 2, 3, 5, 20, 6, 6, 21 };
			json item = { { "label", label }, { "kind", kind < 0 ? 14 : lspKinds[kind] },
						  { "sortText", std::to_string(group) + label }, { "textEdit", { { "range", editRange }, { "newText", label } } } };
			if (!hover.empty())
			{
				std::string detail = hover.substr(0, hover.find('\n'));
				const size_t brace = detail.find(" {");
				if (brace != std::string::npos)
					detail.erase(brace);
				item["detail"] = detail;
				item["documentation"] = { { "kind", "markdown" }, { "value", "```dscript\n" + hover + "\n```" } };
			}
			seen[label] = items.size();
			items.push_back(item);
		};
		auto l_visible = [&](const CS* s) { return !s->isPrivate || privs.count(s->parent) > 0; };
		auto l_addMembers = [&](const std::string& owner, std::initializer_list<int> kinds) {
			auto it = model->membersOf.find(owner);
			if (it == model->membersOf.end())
				return;
			for (const CS* m : it->second)
			{
				if (std::find(kinds.begin(), kinds.end(), m->kind) != kinds.end() && l_visible(m))
					l_add(m->name, m->kind, m->hover, 1);
			}
		};
		auto l_addChildren = [&](const std::string& parent, bool typesOnly) {
			auto it = model->byParent.find(parent);
			if (it == model->byParent.end())
				return;
			for (const CS* s : it->second)
			{
				const bool type = s->kind == CS::Struct || s->kind == CS::Enum || s->kind == CS::Alias || s->kind == CS::Namespace;
				if (l_visible(s) && s->kind != CS::Define && (type || !typesOnly))
					l_add(s->name, s->kind, s->hover, 2);
			}
		};
		// The names usable from the cursor's scope (types only: types and namespaces).
		auto l_addScopes = [&](bool typesOnly) {
			for (const std::string& anc : ancestors)
			{
				const bool inStruct = l_kindOf(anc) == CS::Struct;
				auto it = model->byParent.find(anc);
				if (it == model->byParent.end())
					continue;
				for (const CS* s : it->second)
				{
					const bool type = s->kind == CS::Struct || s->kind == CS::Enum || s->kind == CS::Alias || s->kind == CS::Namespace;
					if ((inStruct && s->kind != CS::Struct && s->kind != CS::Enum) || (typesOnly && !type))
						continue;
					l_add(s->name, s->kind, s->hover, s->kind == CS::Define ? 3 : 2);
				}
			}
		};

		static const char* builtinTypes[] = { "i8", "u8", "i16", "u16", "i32", "u32", "bool", "char", "int", "uint", "string", "void", nullptr };
		static const char* keywords[] = { "var", "const", "sub", "struct", "enum", "alias", "return", "if", "else", "while", "do", "for",
			"break", "continue", "null", "true", "false", "cast", "sizeof", "low", "interrupt", "inline", "static", "priv", "assert",
			"debug_print", "namespace", "defer", "and", "or", "not", nullptr };
		if (where == General || where == TypeName)
		{
			if (where == General)
			{
				for (const auto& l : locals)
					l_add(l.first, l.second->kind, l.second->hover, 0);
			}
			l_addScopes(where == TypeName);
			for (int i = 0; builtinTypes[i] != nullptr; i++)
				l_add(builtinTypes[i], -1, "", 4);
			if (where == General)
			{
				for (int i = 0; keywords[i] != nullptr; i++)
					l_add(keywords[i], -1, "", 4);
			}
		}
		else
		{
			std::vector<Segment> chain;
			bool global = false;
			const size_t end = rest.size() - (where == Dot ? 1 : 2);
			size_t chainStart = 0;
			// sub f() -> Ns::|  (a return type, not '->' on a value)
			static const std::regex scopedReturnType(R"(\bsub\b[^;{}=]*\)\s*->\s*(::)?[A-Za-z_][\w:]*::$)");
			const size_t arrow = rest.rfind("->");
			const bool returnScope = where == Scope && arrow != std::string::npos && std::regex_search(rest, scopedReturnType);
			const bool parsed = returnScope ? parseChain(rest.substr(arrow + 2), end - arrow - 2, chain, global, chainStart)
				: parseChain(rest, end, chain, global, chainStart);
			if (returnScope)
				chainStart += arrow + 2;
			// var x: Ns::|  (types only)
			const std::string lead = rtrim(rest.substr(0, std::min(chainStart, rest.size())));
			const bool typesOnly = where == Scope && parsed && (returnScope || std::regex_search(lead, declType));
			if (where == Scope && !parsed)
			{
				// ::name: the global ones.
				const std::string r = rtrim(rest.substr(0, end));
				if (r.empty() || !isIdentChar(r.back()))
					l_addChildren("", false);
			}
			else if (parsed)
			{
				const Value v = l_evaluate(chain, global);
				if (where == Scope && (v.kind == Value::Namespace || v.kind == Value::StructType || v.kind == Value::EnumType))
					l_addChildren(v.s, typesOnly);
				else if (where == Dot && v.kind == Value::Typed && v.s.rfind("s:", 0) == 0)
					l_addMembers(v.s.substr(2), { CS::Field, CS::Method });
				else if (where == Dot && v.kind == Value::StructType)
					l_addMembers(v.s, { CS::StaticSub });
				else if (where == Dot && v.kind == Value::EnumType)
					l_addMembers(v.s, { CS::EnumMember });
				else if (where == Arrow && v.kind == Value::Typed && v.s.rfind("p:s:", 0) == 0)
					l_addMembers(v.s.substr(4), { CS::Field, CS::Method });
			}
		}
		return { { "isIncomplete", false }, { "items", items } };
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
