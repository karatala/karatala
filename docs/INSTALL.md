# Download and install Karatala

## Download

You need two downloads:

1. [Download the free sample library (about 4Gb .zip)](https://github.com/karatala/sample_library/archive/refs/heads/main.zip). Open the ZIP and extract the sample_folder to a place where you want to keep it.
2. [Download Karatala for your computer](https://github.com/karatala/karatala/releases). Open the newest release. Click **Assets** if the files are hidden.

Look for names like these. The version number can be different:

| Computer | ZIP for manual installation | Installer |
| --- | --- | --- |
| Windows | `Karatala Drum Kit Engine-1.0.0-Windows.zip` | `Karatala Drum Kit Engine-1.0.0-Windows-unsigned.exe` |
| macOS | `Karatala Drum Kit Engine-1.0.0-macOS.zip` | `Karatala Drum Kit Engine-1.0.0-macOS-unsigned.pkg` |
| Linux | `Karatala Drum Kit Engine-1.0.0-Linux.zip` | Use the ZIP |

Choose one Karatala download: the installer or the ZIP. The sample library is a separate download for both choices. **Source code** downloads are for people who want to work on the code.

## Install with an installer

On Windows, open the `.exe`. On macOS, open the `.pkg`. Follow the steps on the screen.

The installer puts the app, plugins, drum layouts, and the Default preset in their folders. Karatala creates its settings file when you first open it. The installer does not install the sample library.

Then follow **Choose sounds and play** below.

## Install from a ZIP

Open the ZIP and copy its files to a normal folder first. Close your music app before you copy plugins.

For use in a music app, VST3 is a good first choice. Copy the whole `Karatala Drum Kit Engine.vst3` file or folder from the ZIP's `VST3` folder. Do not copy only the files inside it.

| Computer | Put the VST3 here | Put `data/kit_layouts` and `data/instrument_map_presets` here |
| --- | --- | --- |
| Windows | `C:\Program Files\Common Files\VST3` | `%PUBLIC%\Documents\Karatala` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` | `/Users/Shared/Karatala` |
| Linux | `~/.vst3` | `~/.config/Karatala` |

Create a folder if it is missing. Copy both folders from the ZIP's `data` folder into `Karatala`. The result must look like this:

```text
Karatala/
    kit_layouts/
        roland-td-17.yml
        ...
    instrument_map_presets/
        Default.txt
```

Keep any layouts or presets you have changed yourself.

On Windows, paste `%PUBLIC%\Documents\Karatala` into the address bar in File Explorer. Windows may ask for permission when you copy a plugin into Program Files.

On macOS, open Finder. Choose **Go > Go to Folder** and paste the path. `~` means your home folder.

On Linux, `~` means your home folder. Press **Ctrl+H** in your file manager to show folders whose names start with a dot. If you use `XDG_CONFIG_HOME`, put the layouts in `Karatala` inside that folder instead of `~/.config`.

To use Karatala without a music app, open the app in the ZIP's `Standalone` folder. On macOS, copy the `.app` to Applications first. On Windows and Linux, you can keep the app in a folder of your choice. On Linux, if needed, open the file's Properties and allow it to run as a program.

If your music app uses CLAP or AU, copy that plugin instead of VST3. Copy the whole file or folder from the ZIP's `CLAP` or `AU` folder:

| Computer | CLAP folder | AU folder |
| --- | --- | --- |
| Windows | `C:\Program Files\Common Files\CLAP` | Not available |
| macOS | `~/Library/Audio/Plug-Ins/CLAP` | `~/Library/Audio/Plug-Ins/Components` |
| Linux | `~/.clap` | Not available |

CLAP files end in `.clap`. AU files end in `.component`. Create the destination folder if it is missing.

## Choose sounds and play

1. Open Karatala. In your music app, add it as an instrument on a track. If it is missing, ask the music app to scan its plugins again.
2. Click **Conf**. This is the settings page.
3. Next to **Sample library**, click **Browse...**. Choose the extracted folder that contains the drum kits. It may be called `sample_library-main`. If no kits appear, check that you chose the folder with the kits, not the folder above it.
4. Choose your drum module under **Default kit layout**. Click **Save layout defaults**, then close and open Karatala again. If your module is not listed, start with the default layout and use **Map** to set the notes.
5. Click **Kit > Presets**, choose **Default**, and click **LOAD PRESET**. Wait for sounds to load. This preset uses several kits from the full sample library. You can also choose one library kit and click **LOAD KIT**.
6. Click a pad to hear a sound. Start with a low speaker or headphone volume.
7. To use e-Drums, connect the drum module to your computer. Select it as a MIDI input in your music app, and turn on the track's input listening. In the separate Karatala app, use its audio/MIDI settings to choose your sound output and MIDI input.

You do not need to write a config file or edit the layout files for these steps.

## Sample library folders

Choose the folder that contains the kits in **Conf > Sample library > Browse...**:

```text
sample_library-main/                 <- choose this folder
    Kit Name by Author [License]/
        kick-22/
            center/
                kick-22-center-127a-dim.flac
```

Keep each kit's folders, sample names, and other files together. Do not put all FLAC files in one folder. To make your own library, see [Sample library format](https://github.com/karatala/karatala/blob/main/docs/SAMPLE_LIBRARY_FORMAT.md).


## If Windows or macOS shows a warning

A file with `unsigned` in its name has no verified publisher signature. Karatala is free and open source. Signing services can cost money.

Only continue if you downloaded the file from the Karatala release page linked above and want to open it.

- Windows: if the warning has **More info**, click it, then **Run anyway**, if available. Some computer settings can block unsigned apps.
- macOS: try to open the app or installer once. Then open **System Settings > Privacy & Security**. Use **Open Anyway** if it is available for Karatala. [Apple's steps](https://support.apple.com/102445)

You do not need to turn off protection for your whole computer.

## Settings folders

Karatala creates `config.yaml` in its data folder. It saves user presets there too:

- Windows: `%PUBLIC%\Documents\Karatala`
- macOS: `/Users/Shared/Karatala`
- Linux: `~/.config/Karatala`, or `$XDG_CONFIG_HOME/Karatala` when set

The sample library can stay in a different folder. If you move it, choose its new folder in **Conf > Sample library > Browse...**.

## Help

[Report a problem](https://github.com/karatala/karatala/issues). Include your operating system, Karatala version, and the steps that caused the problem.
