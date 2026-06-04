cask "ajazz-control-center" do
  version "0.1.1"
  sha256 "51b63dc5ae30c52f09bd81b91a093a5dec6019612622da6bf95414389dbc21f7"

  url "https://github.com/Aiacos/ajazz-control-center/releases/download/v#{version}/ajazz-control-center-#{version}-Darwin.dmg",
      verified: "github.com/Aiacos/ajazz-control-center/"
  name "AJAZZ Control Center"
  desc "Cross-platform control center for AJAZZ devices"
  homepage "https://github.com/Aiacos/ajazz-control-center"

  # The DMG is a Universal (x86_64 + arm64) build published on GitHub Releases.
  # livecheck tracks the newest non-prerelease GitHub tag (strips the leading "v").
  livecheck do
    url :url
    strategy :github_latest
  end

  # The app is currently NOT code-signed or notarized by an Apple Developer ID.
  # Homebrew strips the com.apple.quarantine attribute on cask-installed apps, so
  # `brew install` users typically avoid the Gatekeeper "damaged / cannot be opened"
  # dialog. If macOS still refuses to open it, the caveats below document the manual
  # override. Remove this caveat (and revisit official-repo submission) once the
  # build is signed + notarized.
  app "AJAZZ Control Center.app"

  zap trash: [
    "~/Library/Application Support/AJAZZ Control Center",
    "~/Library/Application Support/io.github.Aiacos.AjazzControlCenter",
    "~/Library/Caches/io.github.Aiacos.AjazzControlCenter",
    "~/Library/HTTPStorages/io.github.Aiacos.AjazzControlCenter",
    "~/Library/Preferences/io.github.Aiacos.AjazzControlCenter.plist",
    "~/Library/Saved Application State/io.github.Aiacos.AjazzControlCenter.savedState",
  ]

  caveats <<~EOS
    #{token} is not signed with an Apple Developer ID nor notarized.

    Homebrew removes the quarantine flag on cask-installed apps, so it should
    launch normally. If macOS still reports the app is damaged or cannot be
    opened, clear the quarantine attribute manually:

      xattr -dr com.apple.quarantine "/Applications/AJAZZ Control Center.app"

    or right-click the app in Finder and choose "Open" the first time.
  EOS
end
