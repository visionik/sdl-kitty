# Homebrew formula template for visionik/homebrew-tap (Formula/sdl-kitty.rb).
#
# After tagging a GitHub release (e.g. `git tag v0.5.0 && git push --tags`),
# copy this file into https://github.com/visionik/homebrew-tap as
# `Formula/sdl-kitty.rb` and fill in the version + SHA256SUMS from the release.
#
# Users install with:
#   brew install visionik/tap/sdl-kitty
# Or build latest main right now (even before the first tagged release):
#   brew install --HEAD visionik/tap/sdl-kitty
class SdlKitty < Formula
  desc "Run SDL3 apps (including MAME) inside Kitty-graphics-capable terminals"
  homepage "https://github.com/visionik/sdl-kitty"
  license "Zlib"
  version "0.5.0"

  # Prebuilt relocatable tarballs from GitHub Releases (fast, no compile needed).
  # Update version + sha256 values from the release SHA256SUMS file.
  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/visionik/sdl-kitty/releases/download/v#{version}/sdl-kitty-#{version}-darwin-arm64.tar.gz"
      sha256 "81ab836201c82ecd7489cd164b6b27bb595900ed3dee2d83e32501eaf3ead19e"
    else
      url "https://github.com/visionik/sdl-kitty/releases/download/v#{version}/sdl-kitty-#{version}-darwin-x86_64.tar.gz"
      sha256 "c40947c6cc4c7b73a742f224302e55d42e90daf3e5363e7bff1853221366f94f"
    end
  end

  on_linux do
    if Hardware::CPU.arm?
      url "https://github.com/visionik/sdl-kitty/releases/download/v#{version}/sdl-kitty-#{version}-linux-arm64.tar.gz"
      sha256 "0e7115d3a522eb044403a19e66aabff464431066eef3b905b0a511321d3ceb74"
    else
      url "https://github.com/visionik/sdl-kitty/releases/download/v#{version}/sdl-kitty-#{version}-linux-x86_64.tar.gz"
      sha256 "7b3e56c15af310a26bbc9a73180e7645b917ffd42e67af4a4981a1dfa686d0bc"
    end
  end

  head do
    url "https://github.com/visionik/sdl-kitty.git", branch: "main"
    depends_on "cmake" => :build
    depends_on "ninja" => :build
  end

  def install
    if build.head?
      system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release",
             "-DSDL_UNIX_CONSOLE_BUILD=ON",
             "-DSDL_X11=OFF",
             "-DSDL_WAYLAND=OFF",
             "-DSDL_KITTY_RELOCATABLE=ON",
             "-DSDL_KITTY_BUILD_SDL_TESTS=OFF",
             *std_cmake_args
      system "cmake", "--build", "build"
      system "cmake", "--install", "build"
    else
      # Prebuilt tarball already has bin/sdlk, lib/sdl-kitty/, share/sdl-kitty/
      prefix.install Dir["*"]
    end
  end

  def caveats
    <<~EOS
      Run the quick setup wizard (checks your terminal, optionally installs MAME
      and free legal arcade ROMs from mamedev.org):
        sdlk setup

      Or jump straight in:
        sdlk demo                  # bundled demos (fireworks, Dear ImGui, mouse_paint)
        sdlk mame-roms             # download free arcade ROMs from mamedev.org
        sdlk mame robby            # play Robby Roto in your terminal
        sdlk doctor                # verify Kitty graphics & keyboard support

      Requires a Kitty-graphics-capable terminal: Ghostty, Kitty, WezTerm, iTerm2, or Rex.
    EOS
  end

  test do
    assert_match "sdlk", shell_output("#{bin}/sdlk --version")
    assert_predicate lib/"sdl-kitty", :directory?
    assert_predicate share/"sdl-kitty/terminals.toml", :exist?
    assert_predicate share/"sdl-kitty/sdl-kitty.toml", :exist?
  end
end
