// The reference tables of osrs/cache/store.h, as RuneLite's fs/Index and
// fs/Archive hold them after Store.load().

#include "lines.h"

#include <string>

namespace osrs::cache::oracle {

namespace {

void indexes(const Store& store, Lines& out) {
    for (const Index index : store.indices()) {
        const int id = static_cast<int>(index);
        out.object({id});
        const IndexInfo* info = store.index(index);
        out.integer("id", id);
        // An index without a reference table keeps fs/Index's defaults.
        out.integer("protocol", info != nullptr ? info->protocol : 6);
        out.boolean("named", info == nullptr || info->named);
        out.boolean("sized", info != nullptr && info->sized);
        out.integer("revision", info != nullptr ? info->revision : 0);
        out.integer("crc", info != nullptr ? static_cast<std::int32_t>(info->crc) : 0);
        out.integer("compression", info != nullptr ? info->compression : 0);
        if (info == nullptr || out.skipping()) continue;
        std::size_t archive = 0;
        for (const auto& [groupId, group] : info->groups) {
            const std::string path = at("archives", archive++);
            out.integer(field(path, "archiveId"), groupId);
            out.integer(field(path, "nameHash"), group.nameHash);
            out.integer(field(path, "crc"), static_cast<std::int32_t>(group.crc));
            out.integer(field(path, "compressedSize"), group.compressedSize);
            out.integer(field(path, "decompressedSize"), group.decompressedSize);
            out.integer(field(path, "revision"), group.revision);
            out.integer(field(path, "compression"), group.compression);
            for (std::size_t file = 0; file < group.fileIds.size(); ++file) {
                const std::string filePath = at(field(path, "fileData"), file);
                out.integer(field(filePath, "id"), group.fileIds[file]);
                // fs/index/FileData's name hash stays 0 where the index names nothing.
                out.integer(field(filePath, "nameHash"), file < group.fileNameHashes.size() ? group.fileNameHashes[file] : 0);
            }
        }
    }
}

const Type indexType("index", indexes);

} // namespace

} // namespace osrs::cache::oracle
