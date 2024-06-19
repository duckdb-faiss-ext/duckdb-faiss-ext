#pragma once


#include "duckdb/main/extension.hpp"
namespace duckdb {

class FaissExtension : public Extension {
public:
	void Load(DuckDB &db) override;
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
