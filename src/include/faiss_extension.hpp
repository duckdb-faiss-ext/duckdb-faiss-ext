#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension.hpp"
namespace duckdb {

struct CreateFunctionData;
using CreateParamHandler = std::function<void(CreateFunctionData &, const Value &)>;

class FaissExtension : public Extension {
public:
	void Load(DuckDB &db) override;
	std::string Name() override;
	std::string Version() const override;
	static void RegisterCreateParameter(const std::string &key, CreateParamHandler handler);
	static std::unordered_map<std::string, CreateParamHandler> create_param_handlers;

	static void RegisterMetricType();
	static void RegisterEfConstruction();
};

} // namespace duckdb
