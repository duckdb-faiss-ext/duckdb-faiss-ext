import duckdb
import pytest
import os


# Get a fresh connection to DuckDB with the faiss extension binary loaded
@pytest.fixture
def duckdb_conn():
    extension_binary = os.getenv('FAISS_EXTENSION_BINARY_PATH')
    if (extension_binary == ''):
        raise Exception('Please make sure the `QUACK_EXTENSION_BINARY_PATH` is set to run the python tests')
    conn = duckdb.connect('', config={'allow_unsigned_extensions': 'true'})
    conn.execute(f"load '{extension_binary}'")
    return conn

def test_loading(duckdb_conn):
    duckdb_conn.execute("CALL faiss_create('flat8', 8, 'Flat');")
    # just test that it works, and faiss is loaded. That is all
