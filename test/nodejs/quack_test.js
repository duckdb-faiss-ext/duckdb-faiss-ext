var duckdb = require('../../duckdb/tools/nodejs');
var assert = require('assert');

describe(`faiss extension`, () => {
    let db;
    let conn;
    before((done) => {
        db = new duckdb.Database(':memory:', {"allow_unsigned_extensions":"true"});
        conn = new duckdb.Connection(db);
        conn.exec(`LOAD '${process.env.FAISS_EXTENSION_BINARY_PATH}';`, function (err) {
            if (err) throw err;
            done();
        });
    });

    it('faiss loads', function (done) {
        done()
    });
});