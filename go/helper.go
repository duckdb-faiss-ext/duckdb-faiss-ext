package faissextcode

import (
	"database/sql"
	"fmt"
	"os"
)

func SetupDatabase() (*sql.DB, error) {
	db, err := sql.Open("duckdb", "?allow_unsigned_extensions=true")
	if err != nil {
		return nil, err
	}

	path := os.Getenv("FAISS_EXTENSION_BINARY_PATH")
	_, err = db.Exec(fmt.Sprintf("LOAD '%s'", path))
	_, err = db.Exec(fmt.Sprintf("LOAD json"))
	return db, err
}
