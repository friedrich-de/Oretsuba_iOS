plugins {
    id("com.android.application")
}

// The signing key of the published builds is not in the repository: the build
// on GitHub gets it from the repository's secrets. Without it a release build
// is signed with this machine's debug key, which installs but cannot update a
// published build (docs/ANDROID.md).
val keystorePath: String? = System.getenv("ORE_KEYSTORE")?.takeIf { it.isNotEmpty() }

android {
    namespace = "io.github.friedrich_de.oretsuba"
    compileSdk = 36
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "io.github.friedrich_de.oretsuba"
        minSdk = 26
        targetSdk = 36
        // The build on GitHub passes its run number, as the iPhone package does.
        versionCode = (System.getenv("ORE_BUILD_NUMBER") ?: "1").toInt()
        versionName = "0.1.1"
        ndk {
            // Phones and tablets, and the emulator on a PC.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    signingConfigs {
        if (keystorePath != null) {
            create("release") {
                storeFile = file(keystorePath)
                storePassword = System.getenv("ORE_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("ORE_KEY_ALIAS")
                keyPassword = System.getenv("ORE_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    lint {
        // The app's own checks of what is new in Android are done by hand when the
        // target version is raised; a one-colour version of the icon would be a new drawing.
        disable += setOf("OldTargetApi", "GradleDependency", "AndroidGradlePluginVersion", "MonochromeLauncherIcon")
        warningsAsErrors = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// The mining kit's files (docs/MINING.md) go into the package as they are, from the
// folder the Apple app takes them from: the rules for undoing inflections and the
// page of the dictionary popup. The notes on where they come from stay out.
abstract class CopyMiningFiles : DefaultTask() {
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val files: DirectoryProperty

    @get:OutputDirectory
    abstract val assets: DirectoryProperty

    @get:Inject
    abstract val fs: FileSystemOperations

    @TaskAction
    fun copy() {
        fs.sync {
            from(files) { exclude("**/*.md") }
            into(assets.dir("mining"))
        }
    }
}

// The notices of the libraries inside the engine go into the package as they
// are, from the one file the Apple app carries too.
abstract class CopyNotices : DefaultTask() {
    @get:InputFile
    abstract val notices: RegularFileProperty

    @get:OutputDirectory
    abstract val assets: DirectoryProperty

    @TaskAction
    fun copy() {
        notices.get().asFile.copyTo(assets.get().file("ThirdPartyNotices.txt").asFile, overwrite = true)
    }
}

androidComponents {
    onVariants { variant ->
        val name = "copy" + variant.name.replaceFirstChar { it.uppercase() } + "Notices"
        val copy = tasks.register<CopyNotices>(name) {
            notices.set(rootProject.file("../app/Resources/ThirdPartyNotices.txt"))
        }
        variant.sources.assets?.addGeneratedSourceDirectory(copy, CopyNotices::assets)
        val miningName = "copy" + variant.name.replaceFirstChar { it.uppercase() } + "MiningFiles"
        val mining = tasks.register<CopyMiningFiles>(miningName) {
            files.set(rootProject.file("../engine/resources/mining"))
        }
        variant.sources.assets?.addGeneratedSourceDirectory(mining, CopyMiningFiles::assets)
    }
}
