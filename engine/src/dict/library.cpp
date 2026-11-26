// The dictionaries the player has.
#include "dict/library.h"
#if ORE_MINING_KIT

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "dict/lookup.h"
#include "dict/yomitan_import.h"
#include "util/log.h"

namespace ore::dict {

namespace {

const char kListFile[] = "dictionaries.json";
const char kExtension[] = ".db";
const char kUnfinished[] = ".importing";

bool ends_with(const std::string &s, const char *suffix) {
	size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

void make_folders(const std::string &path) {
	for (size_t i = 1; i <= path.size(); ++i)
		if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

std::vector<std::string> files_in(const std::string &folder) {
	std::vector<std::string> names;
	if (DIR *dir = opendir(folder.c_str())) {
		while (dirent *entry = readdir(dir)) names.emplace_back(entry->d_name);
		closedir(dir);
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::string new_id() {
	std::random_device device;
	static const char hex[] = "0123456789abcdef";
	std::string id;
	for (int i = 0; i < 8; ++i) {
		uint32_t part = device();
		for (int k = 0; k < 4; ++k) id += hex[(part >> (4 * k)) & 15];
	}
	return id;
}

bool same_dictionary(const DictionaryInfo &installed, const DictionaryInfo &imported) {
	return installed.title == imported.title || (!installed.index_url.empty() && installed.index_url == imported.index_url);
}

}  // namespace

DictionaryLibrary::DictionaryLibrary(std::string folder, std::shared_ptr<const Deinflector> rules)
    : folder_(std::move(folder)), rules_(std::move(rules)) {
	make_folders(folder_);
	for (const std::string &name : files_in(folder_))
		if (ends_with(name, kUnfinished)) ::remove((folder_ + "/" + name).c_str());
	read_list();
}

DictionaryLibrary::~DictionaryLibrary() {
	stop_ = true;
	if (job_.joinable()) job_.join();
}

std::vector<DictionaryLibrary::Installed> DictionaryLibrary::installed() const {
	std::lock_guard<std::mutex> lock(gate_);
	return installed_;
}

ImportState DictionaryLibrary::import_state() const {
	std::lock_guard<std::mutex> lock(gate_);
	return import_;
}

json::Value DictionaryLibrary::lookup(const std::string &text) const {
	// The readers are held until the lookup ends: one replaced or removed meanwhile keeps its file until then.
	std::vector<std::shared_ptr<DictionaryReader>> readers;
	for (const Installed &item : installed())
		if (item.enabled) readers.push_back(item.reader);
	return look_up(text, readers, *rules_);
}

json::Value DictionaryLibrary::styles() const {
	json::Value list = json::Value::array();
	for (const Installed &item : installed()) {
		if (!item.enabled || item.reader->styles().empty()) continue;
		json::Value entry = json::Value::object();
		entry.set("dictionary", item.reader->info().title);
		entry.set("styles", item.reader->styles());
		list.push(std::move(entry));
	}
	return list;
}

bool DictionaryLibrary::start_import(const std::string &source, Fetch fetch, bool remove_zip) {
	std::lock_guard<std::mutex> lock(gate_);
	if (import_.running()) return false;
	if (job_.joinable()) job_.join();
	stop_ = false;
	import_ = ImportState();
	import_.state = "downloading";
	import_.source = source;
	job_ = std::thread([this, source, fetch = std::move(fetch), remove_zip]() mutable { run_import(source, std::move(fetch), remove_zip); });
	return true;
}

void DictionaryLibrary::cancel_import() {
	std::lock_guard<std::mutex> lock(gate_);
	if (import_.running()) stop_ = true;
}

void DictionaryLibrary::wait_import() {
	std::thread job;
	{
		std::lock_guard<std::mutex> lock(gate_);
		job = std::move(job_);
	}
	if (job.joinable()) job.join();
}

void DictionaryLibrary::update(ImportState state) {
	std::lock_guard<std::mutex> lock(gate_);
	import_ = std::move(state);
}

void DictionaryLibrary::run_import(std::string source, Fetch fetch, bool remove_zip) {
	std::string zip;
	std::string unfinished = folder_ + "/" + new_id() + kUnfinished;
	std::string id = unfinished.substr(folder_.size() + 1, 32);
	auto state = [&](const char *name) {
		ImportState s;
		s.state = name;
		s.source = source;
		return s;
	};
	// Tells the state how far it is, at most every hundredth.
	auto progress_of = [&](const char *name) {
		auto shown = std::make_shared<double>(-1);
		return [this, shown, base = state(name)](double value) {
			if (value - *shown < 0.01 && value < 1) return;
			*shown = value;
			ImportState s = base;
			s.progress = std::min(1.0, std::max(0.0, value));
			update(std::move(s));
		};
	};
	try {
		zip = fetch(progress_of("downloading"), stop_);
		if (stop_) throw ImportStopped();
		update(state("importing"));
		DictionaryInfo info = import_yomitan(zip, unfinished, id, progress_of("importing"), stop_);
		install(info, unfinished);
		ImportState done = state("done");
		done.progress = 1;
		done.title = info.title;
		update(std::move(done));
		log_info("mining: imported the dictionary %s (%lld words, %lld frequencies, %lld pitch accents)", info.title.c_str(),
		         (long long)info.terms, (long long)info.frequencies, (long long)info.pitches);
	} catch (const std::exception &error) {
		// Whatever went wrong, the import ends, so that the next one may start.
		log_warn("mining: could not import a dictionary: %s", error.what());
		ImportState failed = state("failed");
		failed.error = error.what();
		update(std::move(failed));
	}
	::remove(unfinished.c_str());
	if (remove_zip && !zip.empty()) ::remove(zip.c_str());
}

void DictionaryLibrary::install(const DictionaryInfo &info, const std::string &unfinished) {
	// Puts an imported dictionary in place. One with the same title, or a newer revision of one from
	// the same address, takes the place of the old one, on or off as it was.
	std::string path = folder_ + "/" + info.id + kExtension;
	if (rename(unfinished.c_str(), path.c_str()) != 0) throw DictError("The imported dictionary could not be put in place.");
	auto reader = DictionaryReader::open(path);
	if (!reader) {
		::remove(path.c_str());
		throw DictError("The imported dictionary could not be opened.");
	}
	std::lock_guard<std::mutex> lock(gate_);
	bool replaced = false;
	for (Installed &item : installed_)
		if (same_dictionary(item.reader->info(), info)) {
			item.reader->retire();
			item.reader = reader;
			replaced = true;
			break;
		}
	if (!replaced) installed_.push_back(Installed{reader, true});
	save_locked();
}

bool DictionaryLibrary::arrange(const std::vector<std::string> &order, const std::vector<std::string> &disabled) {
	std::lock_guard<std::mutex> lock(gate_);
	if (order.size() != installed_.size()) return false;
	std::vector<Installed> next;
	for (const std::string &id : order) {
		for (const Installed &item : next)
			if (item.reader->info().id == id) return false;
		auto found = std::find_if(installed_.begin(), installed_.end(), [&](const Installed &item) { return item.reader->info().id == id; });
		if (found == installed_.end()) return false;
		next.push_back(Installed{found->reader, std::find(disabled.begin(), disabled.end(), id) == disabled.end()});
	}
	installed_ = std::move(next);
	save_locked();
	return true;
}

bool DictionaryLibrary::remove(const std::string &id) {
	std::lock_guard<std::mutex> lock(gate_);
	auto found = std::find_if(installed_.begin(), installed_.end(), [&](const Installed &item) { return item.reader->info().id == id; });
	if (found == installed_.end()) return false;
	found->reader->retire();
	installed_.erase(found);
	save_locked();
	return true;
}

bool DictionaryLibrary::media(const std::string &dictionary, const std::string &path, std::string &data, std::string &type) const {
	type = media_type_of(path);
	if (type.empty()) return false;
	std::shared_ptr<DictionaryReader> reader;
	for (const Installed &item : installed())
		if (item.reader->info().id == dictionary || (!reader && item.reader->info().title == dictionary)) reader = item.reader;
	return reader && reader->media(path, data);
}

void DictionaryLibrary::read_list() {
	std::vector<std::pair<std::string, bool>> entries;
	bool has_list = false;
	{
		std::ifstream in(folder_ + "/" + kListFile, std::ios::binary);
		std::stringstream text;
		text << in.rdbuf();
		json::Value list;
		if (in && json::parse(text.str(), list) && list["dictionaries"].is_array()) {
			has_list = true;
			for (const json::Value &entry : list["dictionaries"].items())
				if (!entry.str("id").empty()) entries.emplace_back(entry.str("id"), entry.boolean("enabled", true));
		} else if (in) {
			log_warn("mining: the list of dictionaries could not be read; the dictionaries are listed anew");
		}
	}
	// A file the list does not name was removed, but could not be deleted then. Without a list,
	// every file is a dictionary, on.
	for (const std::string &name : files_in(folder_)) {
		if (!ends_with(name, kExtension)) continue;
		std::string id = name.substr(0, name.size() - strlen(kExtension));
		if (std::any_of(entries.begin(), entries.end(), [&](const auto &entry) { return entry.first == id; })) continue;
		if (has_list)
			::remove((folder_ + "/" + name).c_str());
		else
			entries.emplace_back(id, true);
	}
	std::vector<std::string> seen;
	for (const auto &entry : entries) {
		if (std::find(seen.begin(), seen.end(), entry.first) != seen.end()) continue;
		seen.push_back(entry.first);
		std::string path = folder_ + "/" + entry.first + kExtension;
		if (!exists(path)) continue;
		auto reader = DictionaryReader::open(path);
		if (reader && reader->info().id == entry.first)
			installed_.push_back(Installed{reader, entry.second});
		else
			log_warn("mining: a dictionary file could not be read and is left out");
	}
}

void DictionaryLibrary::save_locked() const {
	json::Value list = json::Value::object();
	json::Value &items = list.set("dictionaries", json::Value::array());
	for (const Installed &item : installed_) {
		json::Value entry = json::Value::object();
		entry.set("id", item.reader->info().id);
		entry.set("enabled", item.enabled);
		items.push(std::move(entry));
	}
	std::string file = folder_ + "/" + kListFile, next = file + ".new";
	{
		std::ofstream out(next, std::ios::binary | std::ios::trunc);
		out << json::write(list);
	}
	rename(next.c_str(), file.c_str());
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
