// The Android app (docs/MOBILE.md): SDL2's Java activity around the game, compiled by
// packaging/mobile/CMakeLists.txt -- the same sources and flags as the Makefile's SDL build.
import java.security.MessageDigest

plugins {
    id("com.android.application")
}

val repoRoot = rootDir.resolve("../..")

// The identity: include/project_identity.h (a fork changes it there, and nowhere else).
val identity = repoRoot.resolve("include/project_identity.h").readText()
fun identityOf(name: String): String =
    Regex("""#define\s+$name\s+"([^"]+)"""").find(identity)?.groupValues?.get(1)
        ?: error("no $name in include/project_identity.h")
val appId = identityOf("PROJECT_APP_ID")
val appName = identityOf("PROJECT_NAME")
val urlScheme = identityOf("PROJECT_URL_SCHEME")
val siteHost = identityOf("PROJECT_SITE")

// The version: the commit count (versionCode must grow), and the commit.
fun git(vararg args: String): String {
    val p = ProcessBuilder(listOf("git") + args).directory(repoRoot).redirectErrorStream(true).start()
    val out = p.inputStream.bufferedReader().readText().trim()
    p.waitFor()
    return out
}
val commitCount = git("rev-list", "--count", "HEAD").toIntOrNull() ?: 1
val commit = git("rev-parse", "--short=8", "HEAD")

android {
    namespace = "net.openswos.game"
    compileSdk = 36
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = appId
        minSdk = 26
        targetSdk = 36
        versionCode = commitCount
        versionName = "1.0.$commitCount ($commit)"
        resValue("string", "app_name", appName)
        manifestPlaceholders["urlScheme"] = urlScheme
        manifestPlaceholders["siteHost"] = siteHost
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static",
                    "-DSWOS_TEST_RULES=" + (System.getenv("OPENSWOS_TEST_RULES") ?: "ON"))
                targets += "main"
            }
        }
    }

    // A release is signed with OpenSWOS's own key (scripts/build_android.sh: a keystore kept
    // outside the repository). Every APK must be signed with the same one, or an update will not
    // install over the last version.
    signingConfigs {
        val keystore = System.getenv("OPENSWOS_KEYSTORE")
        if (!keystore.isNullOrEmpty()) {
            create("release") {
                storeFile = file(keystore)
                storePassword = System.getenv("OPENSWOS_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("OPENSWOS_KEY_ALIAS")
                keyPassword = System.getenv("OPENSWOS_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            externalNativeBuild { cmake { arguments += listOf("-DCMAKE_BUILD_TYPE=RelWithDebInfo") } }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        resValues = true
    }

    externalNativeBuild {
        cmake {
            path = repoRoot.resolve("packaging/mobile/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true       // (loaded by name from the APK's library folder, as SDL does)
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// The project's own data -- the tracked files of bin/ every drop ships (scripts/stage_runtime.sh;
// never the original game's) -- into the APK's assets, with a manifest beside it: a stamp, then
// "size path" for each file. An APK's assets are not files, so the game copies them out on its
// first start and again when the stamp changes (src/mobile.c).
abstract class StageGameData : DefaultTask() {
    @get:Internal
    abstract val repo: DirectoryProperty
    @get:OutputDirectory
    abstract val staged: DirectoryProperty

    init { outputs.upToDateWhen { false } }     // (what is tracked can change without a file here)

    @TaskAction
    fun stage() {
        val root = repo.get().asFile
        val out = staged.get().asFile
        out.deleteRecursively()
        out.mkdirs()
        val dirs = listOf("data", "grafs", "grafs_aga", "grafs_dos", "grafs_2020", "grafs_arcade",
                          "grafs_jag", "sound", "sound_dos", "data_json", "mods")
        val p = ProcessBuilder(listOf("bash", "scripts/stage_runtime.sh", out.absolutePath) + dirs)
            .directory(root).redirectErrorStream(true).start()
        val log = p.inputStream.bufferedReader().readText()
        if (p.waitFor() != 0) throw GradleException("stage_runtime.sh failed:\n$log")
        val lines = mutableListOf<String>()
        val digest = MessageDigest.getInstance("SHA-256")
        out.walkTopDown().filter { it.isFile }.sortedBy { it.relativeTo(out).invariantSeparatorsPath }.forEach { f ->
            val path = f.relativeTo(out).invariantSeparatorsPath
            lines += "${f.length()} $path"
            digest.update(path.toByteArray())
            digest.update(f.readBytes())
        }
        val stamp = digest.digest().joinToString("") { "%02x".format(it) }
        out.resolve("game.manifest").writeText(stamp + "\n" + lines.joinToString("\n") + "\n")
        logger.lifecycle("staged ${lines.size} files of the game's own data")
    }
}

val stageGameData = tasks.register<StageGameData>("stageGameData") {
    repo.set(repoRoot)
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(stageGameData, StageGameData::staged)
    }
}
