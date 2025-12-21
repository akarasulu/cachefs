class Cachefs < Formula
  desc "FUSE caching filesystem fork of bindfs with SQLite metadata store"
  homepage "https://github.com/akarasulu/cachefs"
  url "https://github.com/akarasulu/cachefs.git", branch: "main"
  version "1.0.0"
  license "GPL-2.0-or-later"
  head "https://github.com/akarasulu/cachefs.git", branch: "main"

  depends_on "pkg-config" => :build
  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build

  on_linux do
    depends_on "fuse3"
  end

  on_macos do
    depends_on macfuse: :cask
  end

  depends_on "sqlite"

  def install
    system "./autogen.sh"
    system "./configure", "--prefix=#{prefix}"
    system "make"
    system "make", "install"
  end

  def caveats
    <<~EOS
      cachefs requires a macOS FUSE implementation. macFUSE will be installed as a cask
      dependency (or install/approve fuse-t from its upstream package). Approve the
      system extension and ensure /etc/fuse.conf contains 'user_allow_other' if you
      need allow_other mounts.
    EOS
  end

  test do
    system "#{bin}/cachefs", "--version"
  end
end
