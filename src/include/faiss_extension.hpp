#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension.hpp"
namespace duckdb {

struct CreateFunctionData;
using CreateParamHandler = std::function<void(CreateFunctionData &, const Value &)>;

class FaissExtension : public Extension {
public:
	void Load(ExtensionLoader &db) override;
	std::string Name() override;
	std::string Version() const override;
	static void RegisterCreateParameter(const Identifier &key, CreateParamHandler handler);
	static identifier_map_t<CreateParamHandler> create_param_handlers;

	static void RegisterMetricType();
};

} // namespace duckdb
