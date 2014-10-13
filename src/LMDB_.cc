/** LMDB Matlab wrapper.
 */
#include <lmdb.h>
#include <memory>
#include <mexplus.h>

using namespace std;
using namespace mexplus;

#define ERROR(...) \
    mexErrMsgIdAndTxt("lmdb:error", __VA_ARGS__)
#define ASSERT(cond, ...) \
    if (!(cond)) mexErrMsgIdAndTxt("lmdb:error", __VA_ARGS__)

namespace mexplus {

// Template specialization of MDB_val to mxArray*. For interoperability, use
// string to keep any data.
template <>
mxArray* MxArray::from(const MDB_val& value) {
  const char* value_data = reinterpret_cast<const char*>(value.mv_data);
  return MxArray::from(string(value_data, value_data + value.mv_size));
}

// Note that MxArray::to() is not trivial due to malloc() and free() issue.
// Perhaps a good approach is to wrap MDB_val to implement a destructor.

} // namespace mexplus

namespace {

// Transaction manager.
class Transaction {
public:
  Transaction() : txn_(NULL) {}
  virtual ~Transaction() { abort(); }
  // Begin the transaction.
  void begin(MDB_env* env, unsigned int flags) {
    abort();
    int status = mdb_txn_begin(env, NULL, flags, &txn_);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  // Commit the transaction.
  void commit() {
    if (txn_) {
      int status = mdb_txn_commit(txn_);
      ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
    }
    txn_ = NULL;
  }
  // Abort the transaction.
  void abort() {
    if (txn_)
      mdb_txn_abort(txn_);
    txn_ = NULL;
  }
  // Get the raw transaction pointer.
  MDB_txn* get() {
    return txn_;
  }

private:
  MDB_txn* txn_;
};

// Cursor container.
class Cursor {
public:
  Cursor() : cursor_(NULL) {}
  virtual ~Cursor() { close(); }
  // Open the cursor.
  void open(MDB_txn *txn, MDB_dbi dbi) {
    close();
    int status = mdb_cursor_open(txn, dbi, &cursor_);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  // Close the cursor.
  void close() {
    if (cursor_)
      mdb_cursor_close(cursor_);
    cursor_ = NULL;
  }
  // Get the raw cursor.
  MDB_cursor* get() { return cursor_; }

private:
  MDB_cursor* cursor_;
};

// Database manager.
class Database {
public:
  Database() : env_(NULL) {
    int status = mdb_env_create(&env_);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  virtual ~Database() { close(); }
  void openEnv(const char* filename, unsigned int flags, mdb_mode_t mode) {
    ASSERT(env_, "MDB_env not created.");
    int status = mdb_env_open(env_, filename, flags, mode);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  void openDBI(MDB_txn* txn, const char* name, unsigned int flags) {
    ASSERT(env_, "MDB_env not opened.");
    int status = mdb_dbi_open(txn, name, flags, &dbi_);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  void close() {
    if (env_) {
      mdb_dbi_close(env_, dbi_);
      mdb_env_close(env_);
    }
    env_ = NULL;
  }
  void setMapsize(size_t mapsize) {
    int status = mdb_env_set_mapsize(env_, mapsize);
    ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  }
  MDB_env* getEnv() { return env_; }
  MDB_dbi getDBI() { return dbi_; }

private:
  MDB_env* env_;
  MDB_dbi dbi_;
};

MEX_DEFINE(new) (int nlhs, mxArray* plhs[],
                 int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 1, 20, "MODE", "FIXEDMAP", "NOSUBDIR",
    "NOSYNC", "RDONLY", "NOMETASYNC", "WRITEMAP", "MAPASYNC", "NOTLS",
    "NOLOCK", "NORDAHEAD", "NOMEMINIT", "REVERSEKEY", "DUPSORT", "INTEGERKEY",
    "DUPFIXED", "INTEGERDUP", "REVERSEDUP", "CREATE", "MAPSIZE");
  OutputArguments output(nlhs, plhs, 1);
  std::unique_ptr<Database> database(new Database);
  ASSERT(database.get() != NULL, "Null pointer exception.");
  database->setMapsize(input.get<int>("MAPSIZE", 10485760));
  string filename(input.get<string>(0));
  mdb_mode_t mode = input.get<int>("MODE", 0664);
  unsigned int flags =
      ((input.get<bool>("FIXEDMAP", false)) ? MDB_FIXEDMAP : 0) |
      ((input.get<bool>("NOSUBDIR", false)) ? MDB_NOSUBDIR : 0) |
      ((input.get<bool>("NOSYNC", false)) ? MDB_NOSYNC : 0) |
      ((input.get<bool>("RDONLY", false)) ? MDB_RDONLY : 0) |
      ((input.get<bool>("NOMETASYNC", false)) ? MDB_NOMETASYNC : 0) |
      ((input.get<bool>("WRITEMAP", false)) ? MDB_WRITEMAP : 0) |
      ((input.get<bool>("MAPASYNC", false)) ? MDB_MAPASYNC : 0) |
      ((input.get<bool>("NOTLS", false)) ? MDB_NOTLS : 0) |
      ((input.get<bool>("NOLOCK", false)) ? MDB_NOLOCK : 0) |
      ((input.get<bool>("NORDAHEAD", false)) ? MDB_NORDAHEAD : 0) |
      ((input.get<bool>("NOMEMINIT", false)) ? MDB_NOMEMINIT : 0);
  database->openEnv(filename.c_str(), flags, mode);
  Transaction transaction;
  flags = ((input.get<bool>("RDONLY", false)) ? MDB_RDONLY : 0);
  transaction.begin(database->getEnv(), flags);
  flags =
      ((input.get<bool>("REVERSEKEY", false)) ? MDB_REVERSEKEY : 0) |
      ((input.get<bool>("DUPSORT", false)) ? MDB_DUPSORT : 0) |
      ((input.get<bool>("INTEGERKEY", false)) ? MDB_INTEGERKEY : 0) |
      ((input.get<bool>("DUPFIXED", false)) ? MDB_DUPFIXED : 0) |
      ((input.get<bool>("INTEGERDUP", false)) ? MDB_INTEGERDUP : 0) |
      ((input.get<bool>("REVERSEDUP", false)) ? MDB_REVERSEDUP : 0) |
      ((input.get<bool>("CREATE", flags == 0)) ? MDB_CREATE : 0);
  database->openDBI(transaction.get(), NULL, flags);
  transaction.commit();
  output.set(0, Session<Database>::create(database.release()));
}

MEX_DEFINE(delete) (int nlhs, mxArray* plhs[],
                    int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 1);
  OutputArguments output(nlhs, plhs, 0);
  Session<Database>::destroy(input.get(0));
}

MEX_DEFINE(get) (int nlhs, mxArray* plhs[],
                 int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 2);
  OutputArguments output(nlhs, plhs, 1);
  Database* database = Session<Database>::get(input.get(0));
  string key_string(input.get<string>(1));
  MDB_val mdb_key = {key_string.size(), const_cast<char*>(key_string.c_str())};
  MDB_val mdb_value = {0, NULL};
  Transaction transaction;
  transaction.begin(database->getEnv(), MDB_RDONLY);
  int status = mdb_get(transaction.get(),
                       database->getDBI(),
                       &mdb_key,
                       &mdb_value);
  ASSERT(status == MDB_SUCCESS || status == MDB_NOTFOUND,
         mdb_strerror(status));
  transaction.commit();
  output.set(0, mdb_value);
}

MEX_DEFINE(put) (int nlhs, mxArray* plhs[],
                 int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 3, 4, "NODUPDATA", "NOOVERWRITE", "RESERVE",
      "APPEND");
  OutputArguments output(nlhs, plhs, 0);
  Database* database = Session<Database>::get(input.get(0));
  unsigned int flags =
      ((input.get<bool>("NODUPDATA", false)) ? MDB_NODUPDATA : 0) |
      ((input.get<bool>("NOOVERWRITE", false)) ? MDB_NOOVERWRITE : 0) |
      ((input.get<bool>("RESERVE", false)) ? MDB_RESERVE : 0) |
      ((input.get<bool>("APPEND", false)) ? MDB_APPEND : 0);
  string key_string(input.get<string>(1));
  string value_string(input.get<string>(2));
  MDB_val mdb_key = {key_string.size(), const_cast<char*>(key_string.c_str())};
  MDB_val mdb_value = {value_string.size(),
                       const_cast<char*>(value_string.c_str())};
  Transaction transaction;
  transaction.begin(database->getEnv(), 0);
  int status = mdb_put(transaction.get(),
                       database->getDBI(),
                       &mdb_key,
                       &mdb_value,
                       flags);
  ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  transaction.commit();
}

MEX_DEFINE(remove) (int nlhs, mxArray* plhs[],
                    int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 2);
  OutputArguments output(nlhs, plhs, 0);
  Database* database = Session<Database>::get(input.get(0));
  string key_string(input.get<string>(1));
  MDB_val mdb_key = {key_string.size(), const_cast<char*>(key_string.c_str())};
  Transaction transaction;
  transaction.begin(database->getEnv(), 0);
  int status = mdb_del(transaction.get(), database->getDBI(), &mdb_key, NULL);
  ASSERT(status == MDB_SUCCESS, mdb_strerror(status));
  transaction.commit();
}

MEX_DEFINE(each) (int nlhs, mxArray* plhs[],
                  int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 2);
  OutputArguments output(nlhs, plhs, 0);
  Database* database = Session<Database>::get(input.get(0));
  Transaction transaction;
  Cursor cursor;
  MDB_val mdb_key;
  MDB_val mdb_value;
  transaction.begin(database->getEnv(), MDB_RDONLY);
  cursor.open(transaction.get(), database->getDBI());
  int status = mdb_cursor_get(cursor.get(), &mdb_key, &mdb_value, MDB_NEXT);
  while (status == MDB_SUCCESS) {
    MxArray key(mdb_key);
    MxArray value(mdb_value);
    mxArray* prhs[] = {const_cast<mxArray*>(input.get(1)),
                       const_cast<mxArray*>(key.get()),
                       const_cast<mxArray*>(value.get())};
    ASSERT(mexCallMATLAB(0, NULL, 3, prhs, "feval") == 0, "Callback failure.");
    status = mdb_cursor_get(cursor.get(), &mdb_key, &mdb_value, MDB_NEXT);
  }
  ASSERT(status == MDB_SUCCESS || status == MDB_NOTFOUND,
         mdb_strerror(status));
  cursor.close();
  transaction.commit();
}

MEX_DEFINE(reduce) (int nlhs, mxArray* plhs[],
                    int nrhs, const mxArray* prhs[]) {
  InputArguments input(nrhs, prhs, 3);
  OutputArguments output(nlhs, plhs, 1);
  Database* database = Session<Database>::get(input.get(0));
  Transaction transaction;
  Cursor cursor;
  MDB_val mdb_key;
  MDB_val mdb_value;
  MxArray accumulation(input.get(2));
  transaction.begin(database->getEnv(), MDB_RDONLY);
  cursor.open(transaction.get(), database->getDBI());
  int status = mdb_cursor_get(cursor.get(), &mdb_key, &mdb_value, MDB_NEXT);
  while (status == MDB_SUCCESS) {
    MxArray key(mdb_key);
    MxArray value(mdb_value);
    mxArray* lhs = NULL;
    mxArray* prhs[] = {const_cast<mxArray*>(input.get(1)),
                       const_cast<mxArray*>(key.get()),
                       const_cast<mxArray*>(value.get()),
                       const_cast<mxArray*>(accumulation.get())};
    ASSERT(mexCallMATLAB(1, &lhs, 4, prhs, "feval") == 0, "Callback failure.");
    accumulation.reset(lhs);
    status = mdb_cursor_get(cursor.get(), &mdb_key, &mdb_value, MDB_NEXT);
  }
  ASSERT(status == MDB_SUCCESS || status == MDB_NOTFOUND,
         mdb_strerror(status));
  cursor.close();
  transaction.commit();
  output.set(0, accumulation.release());
}

} // namespace

// Instance manager for Database.
template class mexplus::Session<Database>;

MEX_DISPATCH
