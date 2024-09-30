#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/dependency_list.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"

#include <sstream>

namespace duckdb {

SchemaCatalogEntry::SchemaCatalogEntry(Catalog &catalog)
    : InCatalogEntry(CatalogType::SCHEMA_ENTRY, catalog, "info.schema") {
}

CatalogTransaction SchemaCatalogEntry::GetCatalogTransaction(ClientContext &context) {
	return CatalogTransaction(catalog, context);
}

optional_ptr<CatalogEntry> SchemaCatalogEntry::CreateIndex(ClientContext &context, CreateIndexInfo &info,
                                                           TableCatalogEntry &table) {
	return CreateIndex(GetCatalogTransaction(context), info, table);
}

SimilarCatalogEntry SchemaCatalogEntry::GetSimilarEntry(CatalogTransaction transaction, CatalogType type,
                                                        const string &name) {
	SimilarCatalogEntry result;
	Scan(transaction.GetContext(), type, [&](CatalogEntry &entry) {
		auto entry_score = StringUtil::SimilarityRating(entry.name, name);
		if (entry_score > result.score) {
			result.score = entry_score;
			result.name = entry.name;
		}
	});
	return result;
}

unique_ptr<CreateInfo> SchemaCatalogEntry::GetInfo() const {
	return nullptr;
}

string SchemaCatalogEntry::ToSQL() const {
	auto create_schema_info = GetInfo();
	return create_schema_info->ToString();
}

} // namespace duckdb
