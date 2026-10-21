package io.github.lakomoor.openpocket

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private val Bg = Color(0xFF0D0F17)
private val Panel = Color(0xFF151925)
private val Panel2 = Color(0xFF1D2232)
private val Line = Color(0xFF2A3046)
private val Text = Color(0xFFEEF0F6)
private val Dim = Color(0xFF8A91A8)
private val Accent = Color(0xFF3A6FF0)
private val Ok = Color(0xFF9BBC0F)
private val PillC = Color(0xFFF2F2EE)

class MainActivity : ComponentActivity() {
    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) { Board.connect(c) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val f = IntentFilter("io.github.lakomoor.openpocket.USB_PERMISSION").apply {
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(usbReceiver, f, RECEIVER_NOT_EXPORTED) else registerReceiver(usbReceiver, f)
        Board.connect(this)
        setContent { App() }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        Board.connect(this)                         // USB device attached
    }

    override fun onDestroy() {
        unregisterReceiver(usbReceiver)
        super.onDestroy()
    }
}

@Composable
fun App() {
    var tab by remember { mutableStateOf(0) }
    val state by Board.state.collectAsState()
    val ctx = LocalContext.current
    MaterialTheme(colorScheme = darkColorScheme(background = Bg, surface = Panel, primary = Accent)) {
        Column(Modifier.fillMaxSize().background(Bg).systemBarsPadding()) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Open Pocket", color = Text, fontWeight = FontWeight.ExtraBold, fontSize = 19.sp)
                Spacer(Modifier.width(10.dp))
                Spacer(Modifier.weight(1f))
                Row(Modifier.clip(RoundedCornerShape(50)).background(Panel2).clickable { Board.connect(ctx) }
                    .padding(horizontal = 12.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(9.dp).clip(CircleShape).background(if (state == Board.State.Connected) Ok else Color(0xFF555555)))
                    Spacer(Modifier.width(8.dp))
                    Text(when (state) {
                        Board.State.Connected -> "connected"
                        Board.State.NeedsPermission -> "allow USB access"
                        Board.State.Error -> "USB error, tap to retry"
                        else -> "plug in the board (OTG)"
                    }, color = Dim, fontSize = 13.sp)
                }
            }
            Box(Modifier.weight(1f)) {
                when (tab) {
                    0 -> RemoteScreen()
                    1 -> UpdateScreen()
                    else -> LogScreen()
                }
            }
            Row(Modifier.fillMaxWidth().padding(12.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
                listOf("Remote", "Update", "Log").forEachIndexed { i, t ->
                    Text(t, color = if (tab == i) Color(0xFF111111) else Dim, fontWeight = if (tab == i) FontWeight.SemiBold else FontWeight.Normal,
                        modifier = Modifier.clip(RoundedCornerShape(50)).background(if (tab == i) PillC else Color.Transparent)
                            .clickable { tab = i }.padding(horizontal = 22.dp, vertical = 9.dp))
                }
            }
        }
    }
}

// ---------------------------------------------------------------- menu pictures
private val PAL = intArrayOf(0x101010, 0x000000, 0x0F380F, 0x242424, 0x3C3C3C, 0x7A7A7A, 0xBEBEBE, 0xFFFFFF,
    0x3A6FF0, 0x9BBC0F, 0xF5C518, 0xE5483C, 0x306230, 0x0E1A33, 0x8BAC0F, 0x000000)

private fun renderCells(font: IntArray, rows: List<String>, cols: Int, w: Int, h: Int, top: Int): ImageBitmap {
    val px = IntArray(w * h) { 0xFF000000.toInt() }
    for (cy in rows.indices) for (cx in 0 until cols) {
        val v = rows[cy].substring(cx * 4, cx * 4 + 4).toInt(16)
        val attr = v shr 8; val ch = v and 0xFF
        val fg = PAL[attr and 15]; val bg = PAL[attr shr 4]
        val code = ch and 0x7F; val ext = ch and 0x80 != 0
        for (py in 0 until 10) {
            val fy = py - 1
            val bits = if (fy < 0 || fy > 7) (if (ext) font[code * 8 + (if (fy < 0) 0 else 7)] else 0) else font[code * 8 + fy]
            for (pxx in 0 until 8) {
                val y = top + cy * 10 + py
                if (y < h) px[y * w + cx * 8 + pxx] = (0xFF shl 24) or (if ((bits shr (7 - pxx)) and 1 == 1) fg else bg)
            }
        }
    }
    return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888).asImageBitmap()
}

@Composable
fun RemoteScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val font = remember { ctx.assets.open("font8x8.hex").bufferedReader().readLines().filter { it.isNotBlank() }.map { it.trim().toInt(16) }.toIntArray() }
    var lcd by remember { mutableStateOf<ImageBitmap?>(null) }
    var osd by remember { mutableStateOf<ImageBitmap?>(null) }
    var auto by remember { mutableStateOf(false) }
    var note by remember { mutableStateOf("") }
    suspend fun shot() {
        val r = withContext(Dispatchers.IO) { Board.menuShot() }
        if (r == null) { note = "No menu open: press MENU first"; return }
        note = ""
        lcd = renderCells(font, r.first, 16, 128, 128, 4)
        osd = renderCells(font, r.second, 42, 336, 240, 0)
    }
    LaunchedEffect(auto) { while (auto) { shot(); delay(2000) } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Screen(lcd, Modifier.weight(0.42f).aspectRatio(1f))
            Screen(osd, Modifier.weight(0.58f).aspectRatio(336f / 240f))
        }
        Row(Modifier.padding(vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Pill("Show the menu") { scope.launch { shot() } }
            Spacer(Modifier.width(12.dp))
            Switch(checked = auto, onCheckedChange = { auto = it })
            Spacer(Modifier.width(6.dp))
            Text("live", color = Dim, fontSize = 13.sp)
        }
        if (note.isNotEmpty()) Text(note, color = Dim, fontSize = 13.sp)
        Spacer(Modifier.height(18.dp))
        // the pad
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Key('U', "▲", Modifier.size(58.dp))
                Row { Key('L', "◀", Modifier.size(58.dp)); Box(Modifier.size(58.dp).padding(3.dp).background(Panel2, RoundedCornerShape(6.dp))); Key('R', "▶", Modifier.size(58.dp)) }
                Key('D', "▼", Modifier.size(58.dp))
            }
            Spacer(Modifier.weight(1f))
            Row(Modifier.rotate(-18f), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                Key('B', "B", Modifier.size(70.dp), round = true)
                Key('A', "A", Modifier.size(70.dp).offset(y = (-14).dp), round = true)
            }
        }
        Spacer(Modifier.height(22.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            Key('S', "SELECT", Modifier.height(40.dp).width(92.dp), pill = true)
            Spacer(Modifier.width(10.dp))
            Key('M', "MENU", Modifier.height(40.dp).width(80.dp), pill = true, color = Accent)
            Spacer(Modifier.width(10.dp))
            Key('T', "START", Modifier.height(40.dp).width(92.dp), pill = true)
        }
        Spacer(Modifier.height(12.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
            Key('X', "RESET GAME", Modifier.height(36.dp).width(130.dp), pill = true)
        }
        Spacer(Modifier.height(20.dp))
    }
}

@Composable
private fun Screen(img: ImageBitmap?, mod: Modifier) {
    Box(mod.clip(RoundedCornerShape(12.dp)).background(Color.Black).border(1.dp, Line, RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
        if (img != null) Image(img, null, Modifier.fillMaxSize(), filterQuality = FilterQuality.None)
    }
}

@Composable
private fun Key(k: Char, label: String, mod: Modifier, round: Boolean = false, pill: Boolean = false, color: Color = Text) {
    var down by remember { mutableStateOf(false) }
    val shape = if (round || pill) RoundedCornerShape(50) else RoundedCornerShape(10.dp)
    Box(mod.padding(3.dp).clip(shape).background(if (down) PillC else Panel2).border(1.dp, Line, shape)
        .pointerInput(k) {
            detectTapGestures(onPress = {
                down = true; Board.press(k, true)
                tryAwaitRelease()
                down = false; Board.press(k, false)
            })
        }, contentAlignment = Alignment.Center) {
        Text(label, color = if (down) Color(0xFF111111) else color, fontWeight = FontWeight.Bold, fontSize = if (pill) 12.sp else 18.sp)
    }
}

@Composable
private fun Pill(text: String, primary: Boolean = false, enabled: Boolean = true, onClick: () -> Unit) {
    Box(Modifier.clip(RoundedCornerShape(50)).background(if (primary) Accent else Color.Transparent)
        .border(1.dp, if (primary) Accent else Line, RoundedCornerShape(50))
        .clickable(enabled = enabled, onClick = onClick).padding(horizontal = 18.dp, vertical = 11.dp)) {
        Text(text, color = if (enabled) Text else Dim, fontWeight = if (primary) FontWeight.SemiBold else FontWeight.Normal)
    }
}

// ---------------------------------------------------------------- update
@Composable
fun UpdateScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var info by remember { mutableStateOf<Release.Info?>(null) }
    var error by remember { mutableStateOf("") }
    var progress by remember { mutableStateOf<Pair<String, Float>?>(null) }
    var result by remember { mutableStateOf("") }
    LaunchedEffect(Unit) {
        try { info = withContext(Dispatchers.IO) { Release.latest() } } catch (e: Exception) { error = e.message ?: "error" }
    }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        val i = info ?: return@rememberLauncherForActivityResult
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            result = ""
            try {
                val done = withContext(Dispatchers.IO) { Release.saveTo(ctx, uri, i) { n, p -> progress = n to p } }
                result = "Saved ${done.joinToString(" and ")}. Put the card in the board: Menu › System › System Update."
            } catch (e: Exception) { result = "Failed: ${e.message}" }
            progress = null
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp)) {
        Text("Update", color = Text, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text("Downloads the latest release and writes it to an SD card. The board installs it from the card: Menu › System › System Update.",
            color = Dim, fontSize = 14.sp, modifier = Modifier.padding(top = 4.dp, bottom = 18.dp))
        Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Panel), shape = RoundedCornerShape(16.dp)) {
            Column(Modifier.padding(18.dp)) {
                when {
                    info != null -> {
                        val i = info!!
                        Text(i.name.ifEmpty { i.tag }, color = Text, fontWeight = FontWeight.SemiBold, fontSize = 17.sp)
                        Text("${i.tag} · ${i.date}", color = Dim, fontSize = 13.sp)
                        Spacer(Modifier.height(10.dp))
                        for (a in i.assets) Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                            Text(a.name, color = Text, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                            Spacer(Modifier.weight(1f))
                            Text("${a.size / 1024} KB", color = Dim, fontSize = 13.sp)
                        }
                    }
                    error.isNotEmpty() -> Text(error, color = Dim)
                    else -> Text("Checking GitHub…", color = Dim)
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Pill("Choose the SD card and save", primary = true, enabled = info?.assets?.isNotEmpty() == true && progress == null) { pick.launch(null) }
        progress?.let { (n, p) ->
            Spacer(Modifier.height(16.dp))
            Text("$n  ${(p * 100).toInt()}%", color = Dim, fontSize = 13.sp)
            Spacer(Modifier.height(6.dp))
            LinearProgressIndicator(progress = { p }, modifier = Modifier.fillMaxWidth().height(10.dp).clip(RoundedCornerShape(50)), color = Accent, trackColor = Panel2)
        }
        if (result.isNotEmpty()) {
            Spacer(Modifier.height(14.dp))
            Text(result, color = if (result.startsWith("Failed")) Color(0xFFFF8A80) else Ok)
        }
        Spacer(Modifier.height(24.dp))
        Text("Pick the root of the card (phone SD slot or a USB card reader).\nFirmware restarts the menu; after a bitstream update switch the board off and on.",
            color = Dim, fontSize = 13.sp)
    }
}

// ---------------------------------------------------------------- log
@Composable
fun LogScreen() {
    val log by Board.log.collectAsState()
    val scroll = rememberScrollState()
    LaunchedEffect(log) { scroll.scrollTo(scroll.maxValue) }
    Column(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
        Text(log.ifEmpty { "The firmware log appears here." }, color = Color(0xFFB9C0D4), fontFamily = FontFamily.Monospace, fontSize = 11.sp,
            modifier = Modifier.fillMaxSize().clip(RoundedCornerShape(14.dp)).background(Color(0xFF07080C)).verticalScroll(scroll).padding(12.dp))
    }
}
