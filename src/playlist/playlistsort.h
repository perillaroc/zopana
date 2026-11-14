#pragma once

enum class SortField {
    FileName, Title, Extension, Folder, Path, FileSize, ModifiedTime, CreatedTime, Duration, Resolution, AddedOrder
};
enum class SortDirection { Ascending, Descending };

struct PlaylistSort {
    SortField field = SortField::FileName;
    SortDirection direction = SortDirection::Ascending;

    bool operator==(const PlaylistSort &) const = default;
    bool needsMedia() const { return field == SortField::Title || field == SortField::Duration || field == SortField::Resolution; }
    bool needsFileInfo() const {
        return needsMedia() || field == SortField::FileSize || field == SortField::ModifiedTime || field == SortField::CreatedTime;
    }
};
