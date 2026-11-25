// Berkeley DB 4.8 smoke test: a transactional environment and a btree database
// round-trip one key across a close and reopen, and the library reports 4.8.30.

#include <db_cxx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0700)
#endif

static const u_int32_t ENV_FLAGS = DB_CREATE | DB_INIT_MPOOL | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG;

int main()
{
    int major = 0, minor = 0, patch = 0;
    DbEnv::version(&major, &minor, &patch);
    if (major != 4 || minor != 8 || patch != 30) {
        std::fprintf(stderr, "bdb48_smoke: version %d.%d.%d, expected 4.8.30\n", major, minor, patch);
        return 1;
    }

    const char* tmp = std::getenv("TMPDIR");
    std::string dir = std::string(tmp && *tmp ? tmp : ".") + "/bdb48_smoke_" + std::to_string((long)getpid());
    if (MKDIR(dir.c_str()) != 0) {
        std::perror("bdb48_smoke: mkdir");
        return 1;
    }

    const char* key = "pink";
    const char* value = "2.4.1.0";
    std::string got;
    try {
        for (int pass = 0; pass < 2; pass++) {
            DbEnv env(0);
            env.open(dir.c_str(), ENV_FLAGS, 0600);
            Db db(&env, 0);
            db.open(nullptr, "smoke.dat", "main", DB_BTREE, pass == 0 ? DB_CREATE : 0, 0600);
            Dbt k((void*)key, (u_int32_t)std::strlen(key));
            if (pass == 0) {
                Dbt v((void*)value, (u_int32_t)std::strlen(value));
                if (db.put(nullptr, &k, &v, 0) != 0) {
                    std::fprintf(stderr, "bdb48_smoke: put failed\n");
                    return 1;
                }
            } else {
                Dbt v;
                v.set_flags(DB_DBT_MALLOC);
                if (db.get(nullptr, &k, &v, 0) != 0) {
                    std::fprintf(stderr, "bdb48_smoke: get failed\n");
                    return 1;
                }
                got.assign((const char*)v.get_data(), v.get_size());
                std::free(v.get_data());
            }
            db.close(0);
            env.close(0);
        }
    } catch (const DbException& e) {
        std::fprintf(stderr, "bdb48_smoke: %s\n", e.what());
        return 1;
    }

    if (got != value) {
        std::fprintf(stderr, "bdb48_smoke: read back \"%s\", expected \"%s\"\n", got.c_str(), value);
        return 1;
    }
    std::printf("bdb48_smoke: OK (BDB %d.%d.%d)\n", major, minor, patch);
    return 0;
}
