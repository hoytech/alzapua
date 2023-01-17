#pragma once

#include <map>
#include <vector>
#include <algorithm>

#include "golpe.h"

struct LMDBOffsets {
    struct Table {
        uint16_t tableId;
        std::string name;
    };

    std::vector<Table> tables;

    struct Offset {
        uint16_t tableId;
        uint8_t type; // 0 = key, 1 = val
        uint64_t start;
        uint64_t size;
        uint64_t ref; // offset of corresponding key or val
    };

    std::vector<Offset> offsets;
};

struct LMDBLoader {
    lmdb::env lmdb_env = lmdb::env::create();

    LMDBLoader(const std::string &dir) {
        lmdb_env.set_max_dbs(256);
        lmdb_env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL * 1024UL);

        lmdb_env.open(dir.c_str(), MDB_RDONLY, 0664);
    }

    LMDBOffsets crawl() {
        auto mapStart = (uint64_t)lmdb_env.get_internal_map().data();

        LMDBOffsets output;

        auto txn = lmdb::txn::begin(lmdb_env, nullptr, MDB_RDONLY);

        std::vector<std::string> tableNames;

        {
            auto dbi = lmdb::dbi::open(txn, nullptr);
            auto cursor = lmdb::cursor::open(txn, dbi);
            std::string_view k, v;

            if (cursor.get(k, v, MDB_FIRST)) {
                do {
                    tableNames.emplace_back(k);
                } while (cursor.get(k, v, MDB_NEXT_NODUP));
            }
        }

        uint64_t currTableId = 0;

        for (const auto &n : tableNames) {
            auto dbi = lmdb::dbi::open(txn, n.c_str());
            auto cursor = lmdb::cursor::open(txn, dbi);
            std::string_view k, v;

            if (cursor.get(k, v, MDB_FIRST)) {
                do {
                    auto keyOffset = (uint64_t)k.data() - mapStart;
                    auto valOffset = (uint64_t)v.data() - mapStart;

                    output.offsets.emplace_back(currTableId, 0, keyOffset, k.size(), valOffset);
                    output.offsets.emplace_back(currTableId, 1, valOffset, v.size(), keyOffset);
                } while (cursor.get(k, v, MDB_NEXT));
            }

            output.tables.emplace_back(currTableId, n);
            currTableId++;
        }

        std::sort(output.offsets.begin(), output.offsets.end(), [](const auto &a, const auto &b){
            return a.start < b.start;
        });

        return output;
    }
};
