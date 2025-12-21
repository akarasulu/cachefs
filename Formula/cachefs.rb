class Cachefs < Formula
  desc "FUSE caching filesystem fork of bindfs with SQLite metadata store"
  homepage "https://github.com/akarasulu/cachefs"
  url "https://github.com/akarasulu/cachefs.git", branch: "main"
  version "1.0.0"
  license "GPL-2.0-or-later"
  head "https://github.com/akarasulu/cachefs.git", branch: "main"

  # TODO: Replace placeholders with real bottle URLs and sha256 once bottled and hosted.
  bottle do
    root_url "https://example.com/cachefs/bottles"
    sha256 cellar: :any, arm64_monterey: "<sha256-arm64-monterey>"
    sha256 cellar: :any, monterey: "<sha256-monterey>"
  end

  depends_on "pkg-config" => :build
  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build

  depends_on "sqlite"

  on_linux do
    depends_on "fuse3"
  end

  def install
    system "./autogen.sh"
    system "./configure", "--prefix=#{prefix}"
    system "make"
    system "make", "install"
  end

  def caveats
    <<~EOS
      cachefs requires a macOS FUSE implementation. Install and approve macFUSE
      (e.g., `brew install --cask fuse`) or fuse-t from its upstream package before use.
      Ensure /etc/fuse.conf contains 'user_allow_other' if you need allow_other mounts.
    EOS
  end

  test do
    system "#{bin}/cachefs", "--version"
  end
end
