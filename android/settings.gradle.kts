// The Android app (docs/ANDROID.md): one application module. The engine's C++
// is compiled from ../engine by app/src/main/cpp/CMakeLists.txt.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Oretsuba"
include(":app")
