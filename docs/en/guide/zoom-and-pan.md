# Zoom, pan, and mouse modes

The player starts in **Zoom and Pan** (缩放拖动) on first launch. Click the mode button in the top bar or press **V** over the video to switch to **Normal Playback** (常规播放).

| Action over the video | Zoom and Pan | Normal Playback |
| --- | --- | --- |
| Scroll the mouse wheel | Scroll up or down to zoom around the pointer. Each notch changes the scale by about 15%, from 1× to 8×. Pointing at a black bar uses the nearest edge of the picture. | Scroll up or down to adjust volume. Each notch changes it by 5 percentage points, from 0% to 100%. Smaller wheel steps adjust it proportionally. |
| Left-click | Use the playback button to pause or resume. | Playback toggles after the system double-click interval confirms a single click. |
| Hold the left button and drag | Pan the picture until it reaches an edge. Release the button or change focus to end the drag. | Dragging past the system drag threshold cancels the click. |
| Double-click | Toggle full screen. | Toggle full screen without pausing or resuming playback. |
| Ctrl+scroll | Zoom around the pointer. | Zoom around the pointer in Normal Playback mode. |

## Zoom and fit to the window

Press **+** or **−** over the video to zoom around the center. The number pad keys work too. Click **Fit to Window** (恢复适窗) or press **R** to recenter the picture at 1×.

Switching modes ends the current gesture. Your framing, volume, and playback state carry over. Resizing the window or toggling full screen keeps the zoom and framing center within the picture bounds. During a session, later files inherit the current framing by default. Enable **Fit the view when opening a new file** (打开新文件时恢复适窗取景) under Picture and Window (画面与窗口) to start each file with a fitted view. The player starts at 1× and centered after a restart. [Picture and window settings](./settings.md#video-and-window)

## Frame the picture with the control window {#control-window-framing}

Open the control window with **F7** and select **Zoom and Pan** (缩放与平移). The tab works in both mouse modes. [Control window](./playback.md#control-window)

1. Pause on the frame you want to inspect. In the Framing (取景) section, drag the slider from 1× to 4×. You can also use the zoom buttons for steps of about 15% or scroll over the drag area. The range is 1× to 8× and the zoom is centered on the main video area.
2. Hold the left button and drag in the same area to pan the picture. Click an arrow for a small move or hold it for continuous movement. The arrows move the picture in screen directions. At a boundary, move in the opposite direction to pan back.
3. Click **Center Picture** (画面居中) between the arrows to center the picture at the current zoom. Click **Fit to Window** to the right of the zoom control to return to a centered 1× view. Both commands retain the current paused frame and rotation.

Zooming, dragging, or resizing in the main window updates the zoom and available directions in the control window. Releasing the button, changing focus, hiding the control window, or opening another file ends an active pan.

### Hold to see the full frame {#hold-full-view}

On the **Zoom and Pan** tab, hold **Hold to See Full Frame** (按住看全图) below the drag area to see a centered 1× view at the current angle. Release it to return to the previous zoom and position. The video keeps playing if it was already playing. If paused, the preview shows the same frame.

To use the command from the video area, search for **Hold to See Full Frame** in **Settings → Shortcuts** (设置 → 快捷键). Record a key combination, click **Set Binding** (设置绑定), and apply the changes. The binding starts empty. With focus back in the video area, hold the assigned key to see the full frame and release it to return. Press **R** to fit the picture directly.

To check where a detail lies, pause the video, press **Alt+K** to rotate it 90° clockwise, zoom in to 8×, and pan the detail to one side. Hold the full-frame control to see where it is in the whole picture, then release to continue inspecting it. Changing focus, hiding the control window, or switching to its Playback tab ends the preview. A framing adjustment during the preview first restores the previous framing and then applies the adjustment. When opening another file, the player uses the framing from before the preview or starts at 1× if the fit-on-open setting is enabled.

### Compare a pinned reference frame {#reference-frame}

1. Pause at the start of an action and frame the picture. Open **F7 → Zoom and Pan → Reference Frame** (缩放与平移 → 参考帧) and click the pin icon, **Pin Current Frame** (固定当前帧).
2. Use the frame step icons on the left to move forward or backward one frame. Click the eye icon to switch between the reference and the current picture. The display shows the reference frame timestamp. The main window's Picture (画面) menu also provides commands to pin, toggle, and clear the reference.
3. Pin again to replace the reference with the current frame. Click the crossed-out pin, **Clear Reference Frame** (清除参考帧), to return to the current picture. A reference belongs to the current session.

Switching views works during playback or while paused and preserves undo seek. Frame stepping, seeking, or holding the full-frame control first returns to the current picture. If the original framing is restored after the preview, the reference is available again. Pin a new reference after changing the zoom, position, angle, or video area size.

**Copy Current View** (复制当前取景) and **Current View Screenshot** (当前取景截图) capture the picture you are looking at. A screenshot of a reference uses that frame's timestamp in its filename. **Full Frame Screenshot** (完整画面截图) captures the full current video frame.

## Rotate the picture {#video-rotation}

Open **Picture → Extra Rotation** (画面 → 额外旋转) or the video context menu to rotate clockwise, rotate counterclockwise, or reset the angle. Over the video, press **Alt+K** to rotate 90° clockwise, **Alt+Shift+K** for 90° counterclockwise, or **Alt+Ctrl+K** to reset extra rotation to 0°. The extra angle is relative to the file's own orientation. The menu and on-screen display show that angle.

Rotation keeps the zoom and the point of interest within the picture boundaries. **R** resets framing to a centered 1× view. The rotation reset sets the extra angle to 0°. Subtitles scale with the picture and may be cropped when you zoom in. The angle persists across files and restarts and can be set before playback. [Picture and window settings](./settings.md#video-and-window)
