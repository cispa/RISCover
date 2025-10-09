package client.armfuzz;

import android.app.Activity
import android.os.Bundle

import android.widget.TextView
import android.widget.ScrollView
import java.io.*
import java.io.IOException;
import kotlin.collections.Map;

class MainActivity : Activity() {  
  
    override fun onCreate(savedInstanceState: Bundle?) {  
        super.onCreate(savedInstanceState)

        val scrollView = ScrollView(this)
        val textView = TextView(this)
        scrollView.addView(textView)
        setContentView(scrollView)

        // try {
        val binaries = arrayOf("diffuzz-client", "lscpu")
        val filesDir = filesDir

        for (binary in binaries) {
            val outFile = File(filesDir, binary)
            val `in`: InputStream = assets.open(binary)
            val out = FileOutputStream(outFile)
            val buffer = ByteArray(1024)
            var read: Int
            while (`in`.read(buffer).also { read = it } != -1) {
                out.write(buffer, 0, read)
            }
            `in`.close()
            out.close()
            outFile.setExecutable(true)
        }

        // TODO: diffuzz-client is not updated when rebuilding
        // we need to remove the app
        // probably something todo with time

        // Start the process
        val processBuilder = ProcessBuilder("/system/bin/sh", "-c", filesDir.path + "/diffuzz-client 192.168.10.133 1337 2>&1")
        val environment = processBuilder.environment()
        environment["PATH"] = environment["PATH"] + ":" + filesDir.path
        val process = processBuilder.start()

        // Function to continuously read and update the TextView
        // fun readStreamAndUpdateUI(reader: BufferedReader) {
        Thread {
            var reader = BufferedReader(InputStreamReader(process.inputStream));
            while (true) {
                var line = reader.readLine();
                if (line == null) {
                    runOnUiThread {
                        textView.append("line == null")
                    }
                    break;
                }
                runOnUiThread {
                    textView.append(line + "\n")
                }
            }
            runOnUiThread {
                textView.append("waiting for process")
            }
            process.waitFor()
            runOnUiThread {
                textView.append("process terminated")
            }
        }.start()

                // }.start()
            // }

            // // Start threads to read stdout and stderr
            // readStreamAndUpdateUI(BufferedReader(InputStreamReader(process.inputStream)))
            // readStreamAndUpdateUI(BufferedReader(InputStreamReader(process.errorStream)))

        // } catch (e: Exception) {
        //     e.printStackTrace()
        // }
    }
}
