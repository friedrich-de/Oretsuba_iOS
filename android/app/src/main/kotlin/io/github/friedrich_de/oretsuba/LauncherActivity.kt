package io.github.friedrich_de.oretsuba

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.ProgressBar
import android.widget.Switch
import android.widget.TextView
import java.io.File
import kotlin.concurrent.thread

/**
 * The first screen: whether the game files are there, how to bring them onto
 * this device, and the options that are set before the game starts.
 */
class LauncherActivity : Activity() {
    private lateinit var paths: GamePaths
    private val main = Handler(Looper.getMainLooper())

    private var gameFolder: File? = null
    private var errorMessage: String? = null
    private var importing = false
    /** Mining (docs/MINING.md): null until the server was asked, else what [Native.miningCheck] said. */
    private var miningState: Int? = null
    private var miningText = ""
    private var miningAsked = 0

    private lateinit var filesStatus: TextView
    private lateinit var filesDetail: TextView
    private lateinit var miningStatus: TextView
    private lateinit var startButton: Button
    private lateinit var optionsCard: View
    private lateinit var keepPlace: Switch
    private lateinit var importTitle: TextView
    private lateinit var importButton: Button
    private lateinit var importProgress: ProgressBar
    private lateinit var importProgressText: TextView
    private lateinit var miningButton: Button
    private lateinit var miningRemoveButton: Button
    private lateinit var errorText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        paths = GamePaths(this)
        FolderImport.removeLeftovers(paths.root)
        setContentView(R.layout.launcher)
        filesStatus = findViewById(R.id.files_status)
        filesDetail = findViewById(R.id.files_detail)
        miningStatus = findViewById(R.id.mining_status)
        startButton = findViewById(R.id.start)
        optionsCard = findViewById(R.id.options_card)
        keepPlace = findViewById(R.id.keep_place)
        importTitle = findViewById(R.id.import_title)
        importButton = findViewById(R.id.import_folder)
        importProgress = findViewById(R.id.import_progress)
        importProgressText = findViewById(R.id.import_progress_text)
        miningButton = findViewById(R.id.import_mining)
        miningRemoveButton = findViewById(R.id.remove_mining)
        errorText = findViewById(R.id.error)

        findViewById<TextView>(R.id.import_by_cable).text =
            getString(R.string.import_by_cable, "Android/data/$packageName/files")
        findViewById<TextView>(R.id.keep_place_note).text =
            getString(R.string.keep_place_note, GamePaths.AUTOSAVE_LINES)
        startButton.setOnClickListener { startGame() }
        keepPlace.isChecked = paths.keepsPlace
        keepPlace.setOnCheckedChangeListener { _, checked ->
            paths.keepsPlace = checked
            show()
        }
        importButton.setOnClickListener {
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_FOLDER)
        }
        miningButton.setOnClickListener {
            val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            startActivityForResult(pick, REQUEST_MINING)
        }
        miningRemoveButton.setOnClickListener { removeMining() }
        findViewById<Button>(R.id.check_again).setOnClickListener {
            errorMessage = null
            refresh()
        }
        refresh()
        // For tests: "am start ... --ez autostart true" goes straight into the game.
        if (savedInstanceState == null && intent.getBooleanExtra("autostart", false)) startGame()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        gameFolder = paths.findGameFolder()
        show()
        checkMining()
    }

    /** Asks the mining server named in mining.txt whether it takes the key; the answer can take some seconds. */
    private fun checkMining() {
        val folder = gameFolder
        val asked = ++miningAsked
        if (folder == null) {
            miningState = null
            miningText = ""
            return
        }
        thread(name = "mining-check") {
            val state = IntArray(1)
            val text = String(Native.miningCheck(folder.path.toByteArray(), state), Charsets.UTF_8)
            main.post {
                if (asked != miningAsked) return@post
                miningState = state[0]
                miningText = text
                show()
            }
        }
    }

    private fun show() {
        val folder = gameFolder
        val found = folder != null
        filesStatus.setText(if (found) R.string.files_found else R.string.files_missing)
        filesStatus.setTextColor(getColor(if (found) R.color.good else R.color.warning))
        filesDetail.text = folder?.path ?: getString(R.string.files_missing_note)

        val mining = miningState
        miningStatus.visibility = if (found && mining != null) View.VISIBLE else View.GONE
        if (mining != null) {
            miningStatus.text = if (mining == 0) getString(R.string.mining_off) else miningText
            miningStatus.setTextColor(getColor(when {
                mining > 0 -> R.color.good
                mining < 0 -> R.color.warning
                else -> R.color.secondary_text
            }))
        }

        startButton.visibility = if (found) View.VISIBLE else View.GONE
        startButton.setText(if (paths.keepsPlace && paths.hasAutosave) R.string.continue_game else R.string.start_game)
        startButton.isEnabled = !importing
        optionsCard.visibility = if (found) View.VISIBLE else View.GONE

        importTitle.setText(if (found) R.string.manage_files else R.string.import_files)
        importButton.isEnabled = !importing
        importProgress.visibility = if (importing) View.VISIBLE else View.GONE
        importProgressText.visibility = if (importing) View.VISIBLE else View.GONE
        miningRemoveButton.visibility = if (paths.miningFile.isFile) View.VISIBLE else View.GONE

        errorText.text = errorMessage
        errorText.visibility = if (errorMessage != null) View.VISIBLE else View.GONE
    }

    private fun startGame() {
        val folder = gameFolder ?: return
        if (importing) return
        errorMessage = null
        show()
        startActivityForResult(Intent(this, GameActivity::class.java).putExtra(GameActivity.GAME_FOLDER, folder.path),
                               REQUEST_GAME)
    }

    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        when (request) {
            REQUEST_GAME -> {
                errorMessage = data?.getStringExtra(GameActivity.ERROR)
                show()
            }
            REQUEST_FOLDER -> data?.data?.takeIf { result == RESULT_OK }?.let { confirmImport(it) }
            REQUEST_MINING -> data?.data?.takeIf { result == RESULT_OK }?.let { importMining(it) }
        }
    }

    // ---------------------------------------------------------------- the game folder

    private fun confirmImport(tree: Uri) {
        val name = try {
            FolderImport.name(this, tree)
        } catch (e: Exception) {
            errorMessage = getString(R.string.import_failed, e.localizedMessage ?: e.javaClass.simpleName)
            show()
            return
        }
        if (!File(paths.root, name).exists()) {
            importFolder(tree, name)
            return
        }
        AlertDialog.Builder(this)
            .setMessage(getString(R.string.import_replace, name))
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.replace) { _, _ -> importFolder(tree, name) }
            .show()
    }

    /** Copies the picked folder into the app's folder, away from the main thread. */
    private fun importFolder(tree: Uri, name: String) {
        importing = true
        errorMessage = null
        importProgress.isIndeterminate = true
        importProgressText.text = getString(R.string.import_copying, name)
        // The copy takes minutes: keep the screen, and with it the app, awake.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        show()
        thread(name = "import") {
            val failure = try {
                FolderImport.copy(applicationContext, tree, paths.root) { done, total ->
                    main.post {
                        importProgress.isIndeterminate = false
                        importProgress.max = 1000
                        importProgress.progress = if (total > 0) (done * 1000 / total).toInt() else 0
                        importProgressText.text = getString(R.string.import_progress, name,
                                                            FolderImport.megabytes(done), FolderImport.megabytes(total))
                    }
                }
                null
            } catch (e: FolderImport.Failure) {
                e.message
            } catch (e: Exception) {
                getString(R.string.import_failed, e.localizedMessage ?: e.javaClass.simpleName)
            }
            main.post {
                importing = false
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                errorMessage = failure
                refresh()
                if (failure == null && gameFolder == null) {
                    errorMessage = getString(R.string.import_no_game)
                    show()
                }
            }
        }
    }

    // ---------------------------------------------------------------- mining.txt

    /** Puts the picked mining.txt next to the game folder (docs/MINING.md). */
    private fun importMining(file: Uri) {
        errorMessage = try {
            val bytes = contentResolver.openInputStream(file)?.use { it.readNBytesCompat(MINING_LIMIT + 1) }
            when {
                bytes == null -> getString(R.string.mining_unreadable)
                bytes.size > MINING_LIMIT || !String(bytes, Charsets.UTF_8).contains("server") ->
                    getString(R.string.mining_not_one)
                else -> {
                    paths.miningFile.writeBytes(bytes)
                    null
                }
            }
        } catch (e: Exception) {
            getString(R.string.import_failed, e.localizedMessage ?: e.javaClass.simpleName)
        }
        refresh()
    }

    private fun removeMining() {
        AlertDialog.Builder(this)
            .setMessage(R.string.mining_remove_question)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.remove) { _, _ ->
                paths.miningFile.delete()
                refresh()
            }
            .show()
    }

    private fun java.io.InputStream.readNBytesCompat(limit: Int): ByteArray {
        val buffer = ByteArray(limit)
        var used = 0
        while (used < limit) {
            val n = read(buffer, used, limit - used)
            if (n < 0) break
            used += n
        }
        return buffer.copyOf(used)
    }

    private companion object {
        const val REQUEST_GAME = 1
        const val REQUEST_FOLDER = 2
        const val REQUEST_MINING = 3
        const val MINING_LIMIT = 64 * 1024
    }
}
