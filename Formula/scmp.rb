class Scmp < Formula
  desc "Terminal music player with a local library, Spotify import and audio visualizer"
  homepage "https://github.com/VuxSussy/SCMP"
  license "MIT"
  head "https://github.com/VuxSussy/SCMP.git", branch: "main"

  depends_on "fftw"
  depends_on "ncurses"
  depends_on :macos

  def install
    system "make", "MARCH=", "FFTW_PREFIX=#{Formula["fftw"].opt_prefix}",
           "NCURSES_PREFIX=#{Formula["ncurses"].opt_prefix}"
    bin.install "build/scmp", "build/scmp-spotify"
  end

  test do
    assert_match "Usage: scmp", shell_output("#{bin}/scmp --help")
  end
end
