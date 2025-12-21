class Cachefs < Formula
  desc "FUSE caching filesystem fork of bindfs with SQLite metadata store"
  homepage "https://github.com/akarasulu/cachefs"
  url "https://github.com/akarasulu/cachefs.git", branch: "master"
  version "1.0.0"
  license "GPL-2.0-or-later"
  head "https://github.com/akarasulu/cachefs.git", branch: "master"

  depends_on "pkg-config" => :build
  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build

  on_linux do
    depends_on "fuse3"
  end

  depends_on "sqlite"

  def install
    if OS.mac?
      macfuse_prefix = HOMEBREW_PREFIX
      # macFUSE (cask) does not ship pkg-config files; generate shims and wire flags for configure.
      pcdir = buildpath/"pkgconfig"
      pcdir.mkpath
      %w[fuse fuse3].each do |name|
        (pcdir/"#{name}.pc").write <<~EOS
          prefix=#{macfuse_prefix}
          exec_prefix=${prefix}
          libdir=${exec_prefix}/lib
          includedir=${prefix}/include
          osxfuse_incdir=${prefix}/include/osxfuse

          Name: #{name}
          Description: macFUSE userspace library
          Version: 3.11.0
          Libs: -L${libdir} -losxfuse -pthread
          Cflags: -I${includedir} -I${osxfuse_incdir}
        EOS
      end
      ENV.prepend_path "PKG_CONFIG_PATH", pcdir
      ENV["PKG_CONFIG_LIBDIR"] = [pcdir, ENV["PKG_CONFIG_LIBDIR"]].compact.join(File::PATH_SEPARATOR)
      ENV.append "LDFLAGS", "-L#{macfuse_prefix}/lib -losxfuse -pthread"
      ENV.append "CPPFLAGS", "-I#{macfuse_prefix}/include -I#{macfuse_prefix}/include/osxfuse"
    end

    system "./autogen.sh"
    args = ["--prefix=#{prefix}"]
    # Prefer the FUSE2-compatible API on macOS; fuse3 pkg-config is not available from the macFUSE cask.
    args += ["--with-fuse3=no", "--with-fuse2=yes", "--with-fuse_t=no"] if OS.mac?
    system "./configure", *args
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
