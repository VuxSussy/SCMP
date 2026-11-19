import json
from pathlib import Path
import runpy
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "scmp-spotify"
URL = "https://open.spotify.com/playlist/5wr9DG59AWCoXMfUqd4KFW"


class SpotifyPlaylistTest(unittest.TestCase):
    def test_import_and_retry_partial_download(self):
        tool = runpy.run_path(str(SCRIPT))
        import_playlist = tool["import_playlist"]
        download_playlist = tool["download_playlist"]
        first = "1 - Artist - First.mp3"
        second = "2 - Artist - Second.mp3"
        retry = False

        def fake_spotdl(path, command, *args):
            staging = Path(path)
            if command == "save":
                tracks = [
                    {"name": "Second", "list_position": 5, "list_length": 2, "list_url": URL},
                    {"name": "First", "list_position": 2, "list_length": 2, "list_url": URL},
                ]
                (staging / "playlist.spotdl").write_text(json.dumps(tracks), encoding="utf-8")
            elif retry:
                self.assertEqual((staging / first).read_bytes(), b"complete first")
                self.assertFalse((staging / second).exists())
                (staging / second).write_bytes(b"complete second")
                (staging / "finished.m3u8").write_text(f"#EXTM3U\n{first}\n{second}\n", encoding="utf-8")
            else:
                (staging / first).write_bytes(b"complete first")
                (staging / second).write_bytes(b"partial second")
                (staging / "finished.m3u8").write_text(f"#EXTM3U\n{first}\n", encoding="utf-8")

        with TemporaryDirectory() as folder, patch.dict(import_playlist.__globals__, spotdl=fake_spotdl):
            home = Path(folder)
            directory = home / "My Playlist"
            with patch.object(Path, "home", return_value=home):
                import_playlist(URL, directory)
                manifest = directory / "playlist.spotdl"
                tracks = json.loads(manifest.read_text(encoding="utf-8"))
                self.assertEqual([(t["name"], t["list_position"]) for t in tracks], [("First", 1), ("Second", 2)])
                with self.assertRaisesRegex(ValueError, "already exists"):
                    import_playlist(URL, directory)
                self.assertEqual(download_playlist(directory), 3)
                self.assertFalse((directory / second).exists())
                retry = True
                args = ["scmp-spotify", "import", URL, "--download", "-o", str(directory)]
                with patch.object(sys, "argv", args):
                    self.assertEqual(tool["main"](), 0)
                args[2] = "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"
                with patch.object(sys, "argv", args), self.assertRaisesRegex(ValueError, "another playlist"):
                    tool["main"]()
                self.assertEqual((directory / first).read_bytes(), b"complete first")
                self.assertEqual((directory / second).read_bytes(), b"complete second")
