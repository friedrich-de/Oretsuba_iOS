// The dictionaries the player has: one file each in a folder of the app, a
// list of their order and which are on, and one import at a time. Lookups
// read the dictionaries that are on, in that order.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dict/deinflector.h"
#include "dict/dictionary_db.h"
#include "util/json.h"

namespace ore::dict {

// What the import that runs or ran last is doing.
struct ImportState {
	std::string state = "idle";  // idle, downloading, importing, done or failed
	std::string source;          // what is imported: a file's name or an address
	double progress = 0;         // from 0 to 1
	std::string title;           // the dictionary's title once it is known
	std::string error;           // why it failed
	bool running() const { return state == "downloading" || state == "importing"; }
};

class DictionaryLibrary {
public:
	struct Installed {
		std::shared_ptr<DictionaryReader> reader;
		bool enabled = true;
	};
	// Gets the zip to import and returns its path: downloads it, telling how far
	// it is, or says where a file already is. Throws DictError when it cannot.
	using Fetch = std::function<std::string(const std::function<void(double)> &progress, const std::atomic<bool> &stop)>;

	// Reads what is in `folder`. An import that the end of the app cut short is thrown away.
	DictionaryLibrary(std::string folder, std::shared_ptr<const Deinflector> rules);
	// Stops an import that runs and waits for it.
	~DictionaryLibrary();
	DictionaryLibrary(const DictionaryLibrary &) = delete;
	DictionaryLibrary &operator=(const DictionaryLibrary &) = delete;

	// The dictionaries in their order.
	std::vector<Installed> installed() const;
	ImportState import_state() const;

	// Looks up the words `text` starts with in the dictionaries that are on (see look_up).
	json::Value lookup(const std::string &text) const;
	// The dictionaries that are on which have a stylesheet: [{"dictionary", "styles"}].
	json::Value styles() const;

	// Starts an import, unless one runs. The zip `fetch` gives is removed when
	// the import ends if `remove_zip`. False when an import runs already.
	bool start_import(const std::string &source, Fetch fetch, bool remove_zip);
	void cancel_import();
	// Waits until no import runs (for tests and for leaving).
	void wait_import();

	// Puts the dictionaries in a new order and turns them on or off. The order
	// must name each dictionary once; false when it does not.
	bool arrange(const std::vector<std::string> &order, const std::vector<std::string> &disabled);
	// Removes a dictionary. False when there is none of that name.
	bool remove(const std::string &id);
	// A picture of a dictionary, by the dictionary's name in the folder or its
	// title. False when it has none at that path.
	bool media(const std::string &dictionary, const std::string &path, std::string &data, std::string &type) const;

private:
	void run_import(std::string source, Fetch fetch, bool remove_zip);
	void install(const DictionaryInfo &info, const std::string &unfinished);
	void read_list();
	void save_locked() const;
	void update(ImportState state);

	std::string folder_;
	std::shared_ptr<const Deinflector> rules_;
	mutable std::mutex gate_;
	std::vector<Installed> installed_;
	ImportState import_;
	std::thread job_;
	std::atomic<bool> stop_{false};
};

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
