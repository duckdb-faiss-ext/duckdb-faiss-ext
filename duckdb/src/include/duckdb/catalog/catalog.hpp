//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/catalog/catalog.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/catalog/catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/atomic.hpp"
#include "duckdb/common/enums/catalog_lookup_behavior.hpp"
#include "duckdb/common/enums/on_entry_not_found.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception/catalog_exception.hpp"
#include "duckdb/common/map.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/optional_ptr.hpp"
#include "duckdb/common/reference_map.hpp"
#include "duckdb/parser/query_error_context.hpp"

#include <functional>

namespace duckdb {
struct DropInfo;
struct BoundCreateTableInfo;
struct AlterTableInfo;
struct CreateTableFunctionInfo;
struct CreateCopyFunctionInfo;
struct CreatePragmaFunctionInfo;
struct CreateFunctionInfo;
struct CreateViewInfo;
struct CreateSequenceInfo;
struct CreateCollationInfo;
struct CreateIndexInfo;
struct CreateTypeInfo;
struct CreateTableInfo;
struct DatabaseSize;
struct MetadataBlockInfo;

class AttachedDatabase;
class ClientContext;
class Transaction;

class AggregateFunctionCatalogEntry;
class CollateCatalogEntry;
class SchemaCatalogEntry;
class TableCatalogEntry;
class ViewCatalogEntry;
class SequenceCatalogEntry;
class TableFunctionCatalogEntry;
class CopyFunctionCatalogEntry;
class PragmaFunctionCatalogEntry;
class CatalogSet;
class DatabaseInstance;
class DependencyManager;

struct CatalogLookup;
struct CatalogEntryLookup;
struct SimilarCatalogEntry;

class Binder;
class LogicalOperator;
class LogicalCreateIndex;
class LogicalCreateTable;
class LogicalInsert;
class LogicalDelete;
class LogicalUpdate;
class CreateStatement;

//! The Catalog object represents the catalog of the database.
class Catalog {
public:
	explicit Catalog(AttachedDatabase &db);
	virtual ~Catalog();

public:
	//! Get the SystemCatalog from the ClientContext
	DUCKDB_API static Catalog &GetSystemCatalog(ClientContext &context);
	//! Get the SystemCatalog from the DatabaseInstance
	DUCKDB_API static Catalog &GetSystemCatalog(DatabaseInstance &db);
	//! Get the specified Catalog from the ClientContext
	DUCKDB_API static Catalog &GetCatalog(ClientContext &context, const string &catalog_name);
	//! Get the specified Catalog from the DatabaseInstance
	DUCKDB_API static Catalog &GetCatalog(DatabaseInstance &db, const string &catalog_name);
	//! Gets the specified Catalog from the database if it exists
	DUCKDB_API static optional_ptr<Catalog> GetCatalogEntry(ClientContext &context, const string &catalog_name);
	//! Get the specific Catalog from the AttachedDatabase
	DUCKDB_API static Catalog &GetCatalog(AttachedDatabase &db);

	virtual bool IsDuckCatalog() {
		return false;
	}
	virtual void Initialize(bool load_builtin) = 0;

	bool IsSystemCatalog() const;
	bool IsTemporaryCatalog() const;

	//! Returns a version number that uniquely characterizes the current catalog snapshot.
	//! If there are transaction-local changes, the version returned is >= TRANSACTION_START, o.w. it is a simple number
	//! starting at 0 that is incremented at each commit that has had catalog changes.
	//! If the catalog does not support versioning, no index is returned.
	DUCKDB_API virtual optional_idx GetCatalogVersion(ClientContext &context) {
		return {}; // don't return anything by default
	}

	//! Returns the catalog name - based on how the catalog was attached
	DUCKDB_API virtual string GetCatalogType() = 0;

	DUCKDB_API CatalogTransaction GetCatalogTransaction(ClientContext &context);

	//! Creates a schema in the catalog.

	//! Drops an entry from the catalog
	DUCKDB_API void DropEntry(ClientContext &context, DropInfo &info);

	//! Returns the schema object with the specified name, or throws an exception if it does not exist
	DUCKDB_API SchemaCatalogEntry &GetSchema(ClientContext &context, const string &name,
	                                         QueryErrorContext error_context = QueryErrorContext());
	DUCKDB_API optional_ptr<SchemaCatalogEntry> GetSchema(ClientContext &context, const string &name,
	                                                      OnEntryNotFound if_not_found,
	                                                      QueryErrorContext error_context = QueryErrorContext());
	//! Overloadable method for giving warnings on ambiguous naming id.tab due to a database and schema with name id
	DUCKDB_API virtual bool CheckAmbiguousCatalogOrSchema(ClientContext &context, const string &name) {
		return !!GetSchema(context, name, OnEntryNotFound::RETURN_NULL);
	}
	DUCKDB_API SchemaCatalogEntry &GetSchema(CatalogTransaction transaction, const string &name,
	                                         QueryErrorContext error_context = QueryErrorContext());
	DUCKDB_API virtual optional_ptr<SchemaCatalogEntry>
	GetSchema(CatalogTransaction transaction, const string &schema_name, OnEntryNotFound if_not_found,
	          QueryErrorContext error_context = QueryErrorContext()) = 0;
	DUCKDB_API static SchemaCatalogEntry &GetSchema(ClientContext &context, const string &catalog_name,
	                                                const string &schema_name,
	                                                QueryErrorContext error_context = QueryErrorContext());
	DUCKDB_API static optional_ptr<SchemaCatalogEntry> GetSchema(ClientContext &context, const string &catalog_name,
	                                                             const string &schema_name,
	                                                             OnEntryNotFound if_not_found,
	                                                             QueryErrorContext error_context = QueryErrorContext());
	//! Scans all the schemas in the system one-by-one, invoking the callback for each entry
	DUCKDB_API virtual void ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) = 0;

	//! Alter an existing entry in the catalog.
	DUCKDB_API void Alter(CatalogTransaction transaction, AlterInfo &info);
	DUCKDB_API void Alter(ClientContext &context, AlterInfo &info);

	virtual DatabaseSize GetDatabaseSize(ClientContext &context) = 0;
	virtual vector<MetadataBlockInfo> GetMetadataInfo(ClientContext &context);

	virtual bool InMemory() = 0;
	virtual string GetDBPath() = 0;

	//! Whether or not this catalog should search a specific type with the standard priority
	DUCKDB_API virtual CatalogLookupBehavior CatalogTypeLookupRule(CatalogType type) const {
		return CatalogLookupBehavior::STANDARD;
	}

	DUCKDB_API vector<reference<SchemaCatalogEntry>> GetSchemas(ClientContext &context);
	DUCKDB_API static vector<reference<SchemaCatalogEntry>> GetSchemas(ClientContext &context,
	                                                                   const string &catalog_name);
	DUCKDB_API static vector<reference<SchemaCatalogEntry>> GetAllSchemas(ClientContext &context);

	virtual void Verify();

	static CatalogException UnrecognizedConfigurationError(ClientContext &context, const string &name);

	//! Autoload the extension required for `configuration_name` or throw a CatalogException
	static void AutoloadExtensionByConfigName(ClientContext &context, const string &configuration_name);
	//! Autoload the extension required for `function_name` or throw a CatalogException
	static bool AutoLoadExtensionByCatalogEntry(DatabaseInstance &db, CatalogType type, const string &entry_name);
	DUCKDB_API static bool TryAutoLoad(ClientContext &context, const string &extension_name) noexcept;

public:
private:
	//! Lookup an entry in the schema, returning a lookup with the entry and schema if they exist
	CatalogEntryLookup TryLookupEntryInternal(CatalogTransaction transaction, CatalogType type, const string &schema,
	                                          const string &name);
	//! Calls LookupEntryInternal on the schema, trying other schemas if the schema is invalid. Sets
	//! CatalogEntryLookup->error depending on if_not_found when no entry is found
	CatalogEntryLookup TryLookupEntry(ClientContext &context, CatalogType type, const string &schema,
	                                  const string &name, OnEntryNotFound if_not_found,
	                                  QueryErrorContext error_context = QueryErrorContext());
	//! Lookup an entry using TryLookupEntry, throws if entry not found and if_not_found == THROW_EXCEPTION
	CatalogEntryLookup LookupEntry(ClientContext &context, CatalogType type, const string &schema, const string &name,
	                               OnEntryNotFound if_not_found, QueryErrorContext error_context = QueryErrorContext());
	static CatalogEntryLookup TryLookupEntry(ClientContext &context, vector<CatalogLookup> &lookups, CatalogType type,
	                                         const string &name, OnEntryNotFound if_not_found,
	                                         QueryErrorContext error_context = QueryErrorContext());
	static CatalogEntryLookup TryLookupEntry(ClientContext &context, CatalogType type, const string &catalog,
	                                         const string &schema, const string &name, OnEntryNotFound if_not_found,
	                                         QueryErrorContext error_context);

	//! Return an exception with did-you-mean suggestion.
	static CatalogException CreateMissingEntryException(ClientContext &context, const string &entry_name,
	                                                    CatalogType type,
	                                                    const reference_set_t<SchemaCatalogEntry> &schemas,
	                                                    QueryErrorContext error_context);

	//! Return the close entry name, the distance and the belonging schema.
	static SimilarCatalogEntry SimilarEntryInSchemas(ClientContext &context, const string &entry_name, CatalogType type,
	                                                 const reference_set_t<SchemaCatalogEntry> &schemas);

	virtual void DropSchema(ClientContext &context, DropInfo &info) = 0;

public:
	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}

	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

} // namespace duckdb