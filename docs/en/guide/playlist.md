# Files and playlists

## Open media from File Explorer or the command line {#launch-media}

In File Explorer, right-click a media file, choose **Open with → Choose another app**, and browse to the extracted `zopana.exe`. You can also drop one or more media files onto `zopana.exe` to start the player. Valid files go into the playlist in the order received, and playback starts with the first item.

Pass one or more paths in PowerShell. Relative paths resolve from the current working directory. Use quotes around paths with spaces.

```powershell
& 'C:\Tools\zopana\zopana.exe' 'D:\影片\示例 01.m2ts'

Set-Location 'D:\影片'
& 'C:\Tools\zopana\zopana.exe' '.\示例 01.m2ts' '.\示例 01.srt' '.\示例 02.opus'
```

## Open and append files

| Action | Playlist behavior |
| --- | --- |
| Click **Open File** (打开文件) or press **Ctrl+O** | Select one or more files to replace the playlist and start the first item. |
| Press **Ctrl+Shift+O** to add files | Append to the playlist while the current item plays. |
| Drop files onto the video area | Replace the playlist by default. Hold **Ctrl** to append. |
| Drop files onto the playlist | Append the files or insert them at the marker. |
| Choose **Add Folder** (添加文件夹) | Scan one directory level in the background, sort naturally by filename, and skip duplicates. You can cancel the scan. |

The default filters for **Open File** and **Add Media Files** (添加媒体文件) include M2TS, WMV, and Opus. When you import media and subtitles together, matching subtitles attach to the first media file added. You can also drop a subtitle onto the player during playback.

## Show and move the playlist

Press **P** or click the playlist button to show or hide it. In a wide window, drag the divider to resize the docked list. In a narrow window or full screen, the list opens as an overlay on the video.

Click **Separate Window** (独立窗口) in the playlist header to float the list in a window you can move and resize. It shares a taskbar item with the main window. The floating list hides when you minimize the main window and reappears when you restore it. To hide it, close the floating window or press **Ctrl+W** with the playlist active.

## Search the playlist {#filter-playlist}

Use the search box at the top of the list to match filenames, full paths, or media titles that have been read. Search is case-insensitive. The list shows the match count and total. Clear the query to restore the full list and the previous selection. With a Chinese input method, use Space or Enter in the search box to choose a candidate.

While filtering, double-click or press **Enter** to play a visible selected item. Press **Delete** to remove visible selected items. Hidden entries still participate in sequential and shuffled playback. Drag reordering and **Ctrl+↑/↓** are disabled while filtering. Files dropped onto the list go to the end of the full queue. Clear the query to insert or reorder at a specific position.

## Open and save local playlists {#local-playlists}

Find these commands in the File (文件) menu or the playlist context menu.

| Command | Result |
| --- | --- |
| **Open Playlist** (打开片单) | Read valid items in playlist order, replace the queue, and start with the first item. If the playlist has no usable entries, keep the current list and playback. |
| **Append Playlist** (追加片单) | Add new items to the end of the queue. If something is already loaded, keep the current item, position, and playback state. If the queue is empty, start the first added item. |
| **Export Playlist** (导出片单) | Save the entire queue in its current order, including items hidden by search. Prefer relative paths on the same drive and use absolute paths across drives. |

Local playlists use UTF-8 `.m3u8` files. Relative paths resolve from the playlist's folder. Keep the same folder structure when moving a playlist and its media. The import summary lists missing, duplicate, unsupported, and network entries. HLS playlists show an import error. You save playlists explicitly and can reopen an exported list even when playback history is off.

## Play, remove, and move entries

Press **Enter** or double-click an item to play it. Press **Delete** to remove items or **Ctrl+↑/↓** to move them. Selecting, adding, or reordering entries lets the current item keep playing or stay paused. Removing the current item stops playback and selects an adjacent item for you to start.

Over the video, press **PageUp/PageDown** to play the previous or next item. Right-click the back or forward button in the bottom bar to do the same.

## Sort, reverse, and shuffle the list {#sort-playlist}

Click **Sort** (排序) in the playlist, or choose **Sort Playlist** (列表排序) in the Playback (播放) menu or the playlist context menu. Choose ascending or descending order and a field.

| Field | Sort order |
| --- | --- |
| Filename or media title | Natural numeric order, such as episode 2 before episode 10. Filename comparisons are case-insensitive. Missing titles use the filename. |
| Extension, containing folder, or full path | Group by extension or containing folder, then compare filenames within each group. Full paths can also be compared directly. |
| File size, modified time, or creation time | Compare bytes or actual timestamps. |
| Duration or resolution | Compare seconds or pixel area. Break resolution ties by width and then height. Resolution applies to video. |
| Order added | Use the order in which files were added during the current session. |

Equal values retain their relative order. Unknown values come last in either direction. **Reverse Current Order** (反转当前顺序) flips the full list. **Shuffle List** (随机打乱列表) shuffles it once. This is a one-time reorder, independent of the Shuffle Playback setting and its history. Sorting, reversing, and shuffling the list preserve the current item and selection. When Shuffle Playback is off, Next Item follows the new order.

The player can read titles, durations, and resolutions from files you have yet to play. The list shows progress while reading. Click **Cancel Sort** (取消排序) to keep the current order. Hover over an item to see available metadata. A change in file size or modification time causes metadata to be read again on the next sort.

Sorting runs when you request it. Appending, inserting, or dragging items places them where you add or drop them. Use **Sort Again** (按上次条件重新排序) to apply the previous sort. The player saves the last field and direction, and starts with an empty list. You can assign shortcuts for sorting in Settings.

## Repeat and shuffle playback {#playback-order}

Open **Playback → Repeat Mode** (播放 → 循环模式) or press **Ctrl+R** over the video to cycle through the repeat modes. Toggle **Shuffle Playback** (随机播放) independently in the Playback menu or with **Ctrl+Shift+R**. The defaults are Repeat Off and Shuffle Off.

The playlist toolbar shows the current repeat mode and shuffle state. Click the repeat button to cycle through the modes or the shuffle button to toggle shuffle. Use the playlist context menu to select a repeat mode directly or toggle shuffle. These commands work for docked, overlay, and separate playlists, even when the list is empty.

| Repeat mode | Shuffle off | Shuffle on |
| --- | --- | --- |
| Off | Play in playlist order and stop at the end. | Stop after playing every item in the current round. |
| One | Repeat the current item. | Repeat the current item. Next Item advances in shuffled order. |
| All | Return to the first item when the list ends. | Start a new shuffled round after the last one. With more than one item, the new round starts with a different item. |

You can switch away from an item that is repeating. A new round starts at the end when you use Repeat All. With shuffle on, Previous Item follows the session's playback history. Next Item can then move forward through that history. The player keeps up to 1,024 such entries. Retry a skipped damaged file from the playlist. Changing repeat mode preserves shuffle history. To start a new round from the current item, turn shuffle off and back on. Added items can join this round. Removed items are taken out of the shuffle candidates and history.

When automatically advancing, the player skips files that failed to open during that attempt. It makes at most as many attempts as there were items when it started to advance. If Repeat One fails to restart a file, the player stays on that item and offers **Retry** (重试).

The repeat mode and shuffle switch are restored on the next launch, even with an empty playlist. These preferences are stored independently of playback history.

An earlier shortcut assigned to **Shuffle Playback** still turns on Repeat All and shuffle together. In Settings, look for **Legacy Shuffle** (旧版随机播放) or assign a shortcut to the new shuffle toggle. Press **F1** to see current bindings.
