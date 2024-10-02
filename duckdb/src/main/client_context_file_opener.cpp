#include "duckdb/main/client_context_file_opener.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"

namespace duckdb {

SettingLookupResult ClientContextFileOpener::TryGetCurrentSetting(const string &key, Value &result) {
	throw Exception(ExceptionType::SETTINGS, "Profiling is not enabled for this connection");
}

// LCOV_EXCL_START
SettingLookupResult ClientContextFileOpener::TryGetCurrentSetting(const string &key, Value &result, FileOpenerInfo &) {
	throw Exception(ExceptionType::SETTINGS, "Profiling is not enabled for this connection");
}

optional_ptr<DatabaseInstance> ClientContextFileOpener::TryGetDatabase() {
	return context.db.get();
}

unique_ptr<CatalogTransaction> FileOpener::TryGetCatalogTransaction(optional_ptr<FileOpener> opener) {
	if (!opener) {
		return nullptr;
	}
	auto context = opener->TryGetClientContext();

	auto db = opener->TryGetDatabase();
	return nullptr;
}

optional_ptr<ClientContext> FileOpener::TryGetClientContext(optional_ptr<FileOpener> opener) {
	if (!opener) {
		return nullptr;
	}
	return opener->TryGetClientContext();
}

optional_ptr<DatabaseInstance> FileOpener::TryGetDatabase(optional_ptr<FileOpener> opener) {
	if (!opener) {
		return nullptr;
	}
	return opener->TryGetDatabase();
}

SettingLookupResult FileOpener::TryGetCurrentSetting(optional_ptr<FileOpener> opener, const string &key,
                                                     Value &result) {
	if (!opener) {
		return SettingLookupResult();
	}
	return opener->TryGetCurrentSetting(key, result);
}

SettingLookupResult FileOpener::TryGetCurrentSetting(optional_ptr<FileOpener> opener, const string &key, Value &result,
                                                     FileOpenerInfo &info) {
	if (!opener) {
		return SettingLookupResult();
	}
	return opener->TryGetCurrentSetting(key, result, info);
}

SettingLookupResult FileOpener::TryGetCurrentSetting(const string &key, Value &result, FileOpenerInfo &info) {
	return this->TryGetCurrentSetting(key, result);
}
// LCOV_EXCL_STOP
} // namespace duckdb
