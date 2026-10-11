// Exports decompiled C for every function that calls a "compatibility-relevant"
// Win32/D3D API or references a relevant string, plus import and string lists.
// Usage (headless): -postScript ExportInteresting.java <outDir>
// @category HOGFixer
import java.io.*;
import java.util.*;
import java.util.regex.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ExportInteresting extends GhidraScript {

    static final Set<String> APIS = new HashSet<>(Arrays.asList(
        // window / display
        "CreateWindowExA", "CreateWindowExW", "AdjustWindowRect", "AdjustWindowRectEx",
        "SetWindowPos", "MoveWindow", "SetWindowLongA", "SetWindowLongW", "ShowWindow",
        "ChangeDisplaySettingsA", "ChangeDisplaySettingsW", "ChangeDisplaySettingsExA",
        "ChangeDisplaySettingsExW", "EnumDisplaySettingsA", "EnumDisplaySettingsW",
        "EnumDisplaySettingsExA", "EnumDisplaySettingsExW", "EnumDisplayDevicesA",
        "EnumDisplayDevicesW", "GetSystemMetrics", "SystemParametersInfoA",
        "SystemParametersInfoW", "GetDeviceCaps", "MonitorFromWindow", "GetMonitorInfoA",
        "GetMonitorInfoW", "GetClientRect", "GetWindowRect", "GetWindowPlacement",
        // cursor / input
        "SetCursor", "ShowCursor", "SetCursorPos", "GetCursorPos", "ClipCursor",
        "ScreenToClient", "ClientToScreen", "LoadCursorA", "LoadCursorW", "LoadImageA",
        "LoadImageW", "CreateIconIndirect", "DrawIconEx", "CreateCursor", "SetCapture",
        "ReleaseCapture", "DirectInput8Create", "GetAsyncKeyState",
        // render / timing
        "Direct3DCreate9", "Direct3DCreate9Ex", "DirectDrawCreate", "DirectDrawCreateEx",
        "timeBeginPeriod", "timeEndPeriod", "timeGetTime", "QueryPerformanceCounter",
        "QueryPerformanceFrequency", "Sleep", "GetTickCount", "SetThreadAffinityMask",
        "SetProcessAffinityMask", "PeekMessageA", "PeekMessageW", "GetMessageW",
        "WaitMessage", "MsgWaitForMultipleObjects", "MsgWaitForMultipleObjectsEx",
        // misc
        "RegOpenKeyExW", "RegOpenKeyExA", "RegQueryValueExW", "RegQueryValueExA",
        "CoCreateInstance", "CoGetClassObject", "LoadLibraryA", "LoadLibraryW",
        "LoadLibraryExW", "GetProcAddress", "WTSRegisterSessionNotification"));

    static final Pattern STR_PAT = Pattern.compile(
        "(?i)full ?screen|windowed|window_|resolution|width|height|cursor|vsync|v_sync|" +
        "refresh|widescreen|aspect|fps|frame|d3d|direct3d|display|monitor|adapter|" +
        "ocx|flash|clsid|dpi|scale|mouse|swap|present|backbuffer|software|hardware");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 ? args[0] : ".");
        outDir.mkdirs();
        String base = currentProgram.getName().replaceAll("[^A-Za-z0-9_.-]", "_");

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();

        // --- imports
        try (PrintWriter pw = new PrintWriter(new FileWriter(new File(outDir, base + "_imports.txt")))) {
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
                pw.println(s.getParentNamespace().getName() + "!" + s.getName());
            }
        }

        // function -> reasons
        Map<Function, Set<String>> hits = new TreeMap<>(Comparator.comparing(Function::getEntryPoint));

        // --- API callers
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            if (f.isThunk()) continue;
            for (Function callee : f.getCalledFunctions(monitor)) {
                Function real = callee.isThunk() ? callee.getThunkedFunction(true) : callee;
                String n = real != null ? real.getName() : callee.getName();
                if (APIS.contains(n)) {
                    hits.computeIfAbsent(f, k -> new TreeSet<>()).add("api:" + n);
                }
            }
        }

        // --- strings
        try (PrintWriter pw = new PrintWriter(new FileWriter(new File(outDir, base + "_strings.txt")))) {
            DataIterator it = listing.getDefinedData(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String sv = v.toString();
                if (sv.length() < 3) continue;
                ReferenceIterator refs = rm.getReferencesTo(d.getAddress());
                List<String> fnames = new ArrayList<>();
                boolean interesting = STR_PAT.matcher(sv).find();
                while (refs.hasNext()) {
                    Reference r = refs.next();
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f != null) {
                        fnames.add(f.getEntryPoint().toString());
                        if (interesting && sv.length() < 200) {
                            String flat = sv.replaceAll("\\s+", " ");
                            hits.computeIfAbsent(f, k -> new TreeSet<>())
                                .add("str:" + flat.substring(0, Math.min(60, flat.length())));
                        }
                    }
                }
                pw.println(d.getAddress() + "\t" + String.join(",", fnames) + "\t" +
                    sv.replace("\n", "\\n").replace("\r", "\\r"));
            }
        }

        // --- decompile
        DecompInterface di = new DecompInterface();
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.setSimplificationStyle("decompile");
        di.openProgram(currentProgram);
        int n = 0;
        try (PrintWriter pw = new PrintWriter(new FileWriter(new File(outDir, base + "_interesting.c")))) {
            pw.println("// " + hits.size() + " interesting functions in " + currentProgram.getName());
            for (Map.Entry<Function, Set<String>> e : hits.entrySet()) {
                if (monitor.isCancelled()) break;
                Function f = e.getKey();
                pw.println("\n// ======================================================================");
                pw.println("// " + f.getName() + " @ " + f.getEntryPoint() + "  size=" + f.getBody().getNumAddresses());
                pw.println("// reasons: " + String.join(" | ", e.getValue()));
                StringBuilder callers = new StringBuilder();
                for (Function c : f.getCallingFunctions(monitor)) callers.append(c.getEntryPoint()).append(' ');
                pw.println("// callers: " + callers);
                DecompileResults res = di.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    pw.println(res.getDecompiledFunction().getC());
                } else {
                    pw.println("// decompile failed: " + (res != null ? res.getErrorMessage() : "null"));
                }
                n++;
            }
        }
        di.dispose();

        // --- function list
        try (PrintWriter pw = new PrintWriter(new FileWriter(new File(outDir, base + "_functions.txt")))) {
            for (Function f : fm.getFunctions(true)) {
                pw.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + f.getName());
            }
        }
        println("ExportInteresting: wrote " + n + " functions for " + base);
    }
}
