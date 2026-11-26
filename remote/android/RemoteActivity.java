package org.modulez.remote.preview;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.net.*;
import java.security.MessageDigest;
import java.security.cert.*;
import java.util.*;
import java.util.concurrent.*;
import javax.net.ssl.*;

public class RemoteActivity extends Activity {
    private final Handler ui = new Handler();
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private LinearLayout root;
    private EditText address, code;
    private TextView status, title, format, volumeText;
    private Button pair, scan, toggle;
    private SeekBar volume;
    private ArrayAdapter<String> tracks;
    private SharedPreferences prefs;
    private Session session;
    private long revision = -1;
    private int queueOffset, wantedOffset;
    private Button earlier, later;
    private TextView range;
    private LinearLayout queueNav;
    private boolean foreground, busy, dragging;
    private final Runnable poll = () -> refresh(null, -1);

    private static class Session {
        final String host, pin, token;
        Session(String host, String pin, String token) {
            this.host = host; this.pin = pin; this.token = token;
        }
    }

    private static class HttpError extends IOException {
        final int status;
        HttpError(int status) { super("Player returned " + status); this.status = status; }
    }

    private static String digest(X509Certificate certificate) throws CertificateException {
        try {
            StringBuilder out = new StringBuilder();
            for (byte b : MessageDigest.getInstance("SHA-256").digest(certificate.getEncoded()))
                out.append(String.format(Locale.US, "%02x", b & 255));
            return out.toString();
        } catch (Exception ex) { throw new CertificateException(ex); }
    }

    private static JSONObject call(Session s, String path, String method, String secret,
                                   int value, long revision) throws Exception {
        final String[] fullPin = new String[1];
        X509TrustManager trust = new X509TrustManager() {
            public void checkClientTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                throw new CertificateException("Client certificate unsupported");
            }
            public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
            public void checkServerTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                if (chain == null || chain.length == 0) throw new CertificateException("Missing certificate");
                String fingerprint = digest(chain[0]);
                if (!fingerprint.startsWith(s.pin)) throw new CertificateException("Pairing fingerprint changed");
                fullPin[0] = fingerprint;
            }
        };
        SSLContext tls = SSLContext.getInstance("TLS");
        tls.init(null, new TrustManager[]{trust}, null);
        HttpsURLConnection c = (HttpsURLConnection)new URL("https", s.host, 48788, path)
                .openConnection(Proxy.NO_PROXY);
        try {
            c.setSSLSocketFactory(tls.getSocketFactory());
            // The code on the player authenticates its self-signed certificate.
            c.setHostnameVerifier((host, ssl) -> host.equals(s.host));
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(3500); c.setReadTimeout(5000);
            c.setUseCaches(false); c.setRequestMethod(method);
            if (secret != null) c.setRequestProperty("X-Pair-Code", secret);
            if (!s.token.isEmpty()) c.setRequestProperty("X-Remote-Token", s.token);
            if (value >= 0) c.setRequestProperty("X-Value", Integer.toString(value));
            if (revision >= 0) c.setRequestProperty("X-Revision", Long.toString(revision));
            int result = c.getResponseCode();
            if (result < 200 || result >= 300) throw new HttpError(result);
            if (result == 204) return new JSONObject();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = c.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) bytes.write(buffer, 0, n);
            }
            JSONObject obj = new JSONObject(bytes.toString("UTF-8"));
            if (secret != null) obj.put("pin", fullPin[0]);
            if (c.getHeaderField("X-Fullscreen")!=null) obj.put("fullscreen","1".equals(c.getHeaderField("X-Fullscreen")));
            return obj;
        } finally { c.disconnect(); }
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        prefs = getSharedPreferences("pair", MODE_PRIVATE);
        if (prefs.contains("token")) session = new Session(prefs.getString("host",""),
                prefs.getString("pin",""), prefs.getString("token",""));
        render();
        if (android.os.Build.VERSION.SDK_INT>=30 && saved==null) showLastCrash();
    }

    @android.annotation.TargetApi(30)
    private void showLastCrash() {
        android.app.ActivityManager manager=(android.app.ActivityManager)getSystemService(ACTIVITY_SERVICE);
        if (manager==null) return;
        java.util.List<android.app.ApplicationExitInfo> exits=manager.getHistoricalProcessExitReasons(getPackageName(),0,1);
        if (exits.isEmpty()) return;
        android.app.ApplicationExitInfo exit=exits.get(0);
        if (exit.getTimestamp()<=prefs.getLong("lastExitShown",0)) return;
        String reason;
        switch (exit.getReason()) {
            case android.app.ApplicationExitInfo.REASON_CRASH: reason="App crash"; break;
            case android.app.ApplicationExitInfo.REASON_CRASH_NATIVE: reason="Native crash"; break;
            case android.app.ApplicationExitInfo.REASON_LOW_MEMORY: reason="Low memory"; break;
            case android.app.ApplicationExitInfo.REASON_ANR: reason="App stopped responding"; break;
            default: return;
        }
        prefs.edit().putLong("lastExitShown",exit.getTimestamp()).apply();
        String details=reason+" · "+java.text.DateFormat.getDateTimeInstance().format(new Date(exit.getTimestamp()));
        if (exit.getDescription()!=null) details+="\n"+exit.getDescription();
        final String report=details;
        new android.app.AlertDialog.Builder(this).setTitle("Previous Android exit").setMessage(report)
            .setNegativeButton("Close",null).setPositiveButton("Copy details",(d,w) -> {
                android.content.ClipboardManager clipboard=(android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
                clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Modulez Remote",report));
            }).show();
    }

    private int dp(int n) { return (int)(n * getResources().getDisplayMetrics().density + .5f); }
    private TextView label(LinearLayout box, String text, int size) {
        TextView v = new TextView(this); v.setTextColor(Color.rgb(228,235,235));
        v.setTextSize(size); v.setText(text); box.addView(v); return v;
    }
    private Button button(LinearLayout box, String text, Runnable action) {
        Button b = new Button(this); b.setText(text); b.setAllCaps(false);
        b.setOnClickListener(v -> action.run()); box.addView(b); return b;
    }
    private EditText input(String hint) {
        EditText field = new EditText(this); field.setSingleLine(true); field.setHint(hint);
        field.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        root.addView(field); return field;
    }
    private void render() {
        root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(17,21,23));
        root.setPadding(dp(16),dp(12),dp(16),dp(8));
        root.setOnApplyWindowInsetsListener((v,i) -> {
            v.setPadding(dp(16)+i.getSystemWindowInsetLeft(), dp(12)+i.getSystemWindowInsetTop(),
                    dp(16)+i.getSystemWindowInsetRight(), dp(8)+i.getSystemWindowInsetBottom());
            return i;
        });
        setContentView(root);
        if (session == null) {
            label(root,"Modulez Remote",28);
            label(root,"Choose Pair remote in Modulez Player, then scan the QR code or enter the details below.",16);
            scan = button(root,"Scan QR code",() -> startActivityForResult(
                    new android.content.Intent(this, ScanActivity.class), 41));
            address = input("Player address"); address.setText(prefs.getString("host",""));
            code = input("Pairing code");
            pair = button(root,"Pair with player",this::pair);
            status = label(root,"",15);
        } else {
            label(root,"Now Playing",16); title = label(root,"Connecting…",24);
            title.setMaxLines(2); format = label(root,"",16); format.setMaxLines(2);
            LinearLayout row = new LinearLayout(this); root.addView(row);
            button(row,"Previous",() -> refresh("/previous",-1));
            toggle = button(row,"Play",() -> refresh("/toggle",-1));
            button(row,"Next",() -> refresh("/next",-1));
            volumeText = label(root,"Volume",16); volume = new SeekBar(this); root.addView(volume);
            volume.setMax(100);
            volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onStartTrackingTouch(SeekBar bar) { dragging=true; }
                public void onStopTrackingTouch(SeekBar bar) { dragging=false; refresh("/volume",bar.getProgress()); }
                public void onProgressChanged(SeekBar bar,int n,boolean user) { volumeText.setText("Player volume  " + n + "%"); }
            });
            LinearLayout tools=new LinearLayout(this); root.addView(tools);
            button(tools,"Browse music",() -> music(null,"","Shared music folders"));
            button(tools,"Visualizer",() -> visualizers(0));
            LinearLayout browse = new LinearLayout(this);
            browse.setGravity(android.view.Gravity.CENTER_VERTICAL);
            root.addView(browse);
            label(browse,"Playlist",20).setLayoutParams(new LinearLayout.LayoutParams(0,-2,1));
            button(browse,"Stations",this::chooseStation);
            button(browse,"World Radio",() -> new WorldRadio(session).open());
            queueNav=new LinearLayout(this); root.addView(queueNav);
            earlier=button(queueNav,"Previous page",() -> changeQueuePage(-150));
            range=label(queueNav,"",14);
            later=button(queueNav,"Next page",() -> changeQueuePage(150));
            queueNav.setVisibility(android.view.View.GONE);
            ListView list = new ListView(this);
            root.addView(list,new LinearLayout.LayoutParams(-1,0,1));
            tracks = new ArrayAdapter<>(this,android.R.layout.simple_list_item_1,new ArrayList<>());
            list.setAdapter(tracks); list.setOnItemClickListener((a,v,i,id) -> refresh("/select",queueOffset+i));
            status = label(root,"Connecting to " + session.host,14);
            button(root,"Forget player",this::forget);
        }
    }
    private interface Reply { void show(JSONObject result) throws Exception; }

    private void playerRequest(String path,String method,Reply reply) {
        final Session target=session;
        if (target==null) return;
        worker.execute(() -> {
            try {
                JSONObject result=call(target,path,method,null,-1,-1);
                ui.post(() -> {
                    if (target!=session || !foreground || isFinishing() || isDestroyed()) return;
                    try { reply.show(result); }
                    catch (Exception ex) { notice("Could not read the player's response."); }
                });
            } catch (Exception ex) { ui.post(() -> {
                if (target==session && foreground && !isDestroyed()) notice(message(ex));
            }); }
        });
    }

    private void notice(String text) { Toast.makeText(this,text,Toast.LENGTH_LONG).show(); }
    private String musicQuery(String rootId,String path) {
        return rootId==null ? "" : "?root="+android.net.Uri.encode(rootId)+"&path="+android.net.Uri.encode(path);
    }

    private void music(String rootId,String path,String heading) {
        music(rootId,path,heading,0);
    }
    private void music(String rootId,String path,String heading,int offset) {
        String query=musicQuery(rootId,path)+(rootId==null?"?":"&")+"offset="+offset;
        playerRequest("/music"+query,"GET",data -> {
            JSONArray rows=data.getJSONArray("items");
            int prev=offset>0?1:0;
            String[] names=new String[prev+rows.length()+(data.optBoolean("more")?1:0)];
            if (prev==1) names[0]="‹ Previous files";
            if (data.optBoolean("more")) names[names.length-1]="Next files ›";
            for (int i=0;i<rows.length();i++) {
                JSONObject row=rows.getJSONObject(i);
                names[i+prev]=row.getString("name")+(row.getBoolean("folder")?" /":"");
            }
            android.app.AlertDialog.Builder box=new android.app.AlertDialog.Builder(this)
                .setTitle(heading).setNegativeButton("Close",null);
            if (rootId!=null) box.setNeutralButton("Up",(d,w) -> {
                if (path.isEmpty()) music(null,"","Shared music folders");
                else {
                    int slash=path.lastIndexOf('/');
                    String parent=slash<0 ? "" : path.substring(0,slash);
                    music(rootId,parent,parent.isEmpty()?"Add to playlist":parent);
                }
            });
            if (rootId!=null) box.setPositiveButton("Add entire folder",(d,w) ->
                playerRequest("/music/folder"+musicQuery(rootId,path),"POST",done ->
                    notice("Adding this folder and its subfolders to the playlist")));
            if (names.length==0) box.setMessage(rootId==null ?
                    "Choose shared music folders in the computer’s pairing dialog." : "No music or subfolders here.");
            else box.setItems(names,(d,index) -> {
                if (index<prev) { music(rootId,path,heading,Math.max(0,offset-150)); return; }
                if (index-prev>=rows.length()) { music(rootId,path,heading,offset+rows.length()); return; }
                JSONObject row=rows.optJSONObject(index-prev);
                String child=row.optString("key"), name=row.optString("name");
                if (rootId==null) music(child,"","Add to playlist · "+name);
                else if (row.optBoolean("folder")) music(rootId,child,"Add to playlist · "+name);
                else playerRequest("/music/add"+musicQuery(rootId,child),"POST",added -> {
                    notice("Added "+name); music(rootId,path,heading,offset);
                });
            });
            box.show();
        });
    }

    private int visRequest;
    private void visualizers(int attempt) {
        final int ticket=++visRequest;
        final Session target=session;
        playerRequest("/vis","GET",data -> {
            if (ticket!=visRequest) return;
            if (!data.optBoolean("ready")) {
                if (attempt==0) notice("Opening the desktop visualizer…");
                if (attempt<15) ui.postDelayed(() -> {
                    if (foreground && target==session && ticket==visRequest) visualizers(attempt+1);
                },650);
                else notice("Check the Vis page on the computer and try again.");
                return;
            }
            boolean full=data.optBoolean("fullscreen");
            new android.app.AlertDialog.Builder(this).setTitle("Desktop visualizer")
                .setItems(new String[]{"Wavescope modes","projectM presets",full?"Exit fullscreen":"Enter fullscreen"},(d,i) -> {
                    if (i==2) playerRequest("/vis/fullscreen?on="+(full?"0":"1"),"POST",result -> {});
                    else visChoices(data,i==0?"standard":"projectm");
                }).setNegativeButton("Close",null).show();
        });
    }
    private void visChoices(JSONObject data,String group) {
        JSONArray rows=data.optJSONArray(group);
        if (rows==null || rows.length()==0) { notice("No choices available."); return; }
        String[] names=new String[rows.length()];
        int selected=-1;
        for (int i=0;i<names.length;i++) {
            JSONObject item=rows.optJSONObject(i);
            names[i]=item.optString("name");
            if (group.equals(data.optString("engine")) && item.optString("key").equals(data.optString("selected"))) selected=i;
        }
        new android.app.AlertDialog.Builder(this).setTitle(group.equals("standard")?"Wavescope modes":"projectM presets")
            .setSingleChoiceItems(names,selected,(d,i) -> {
                d.dismiss();
                String key=rows.optJSONObject(i).optString("key");
                playerRequest("/vis?group="+group+"&choice="+android.net.Uri.encode(key),"POST",result -> {
                    if (!result.optBoolean("ready") || !key.equals(result.optString("selected")))
                        notice("The player could not switch to that visualizer.");
                    else notice(names[i]);
                });
            }).setNegativeButton("Close",null).show();
    }

    private void chooseStation() {
        final Session target=session;
        worker.execute(() -> {
            try {
                JSONArray list=call(target,"/stations","GET",null,-1,-1).getJSONArray("stations");
                String[] names=new String[list.length()], ids=new String[list.length()];
                for (int i=0;i<names.length;i++) {
                    JSONObject station=list.getJSONObject(i);
                    names[i]=station.getString("group")+"  ·  "+station.getString("name");
                    ids[i]=station.getString("id");
                }
                ui.post(() -> {
                    if (!foreground || session!=target || isDestroyed()) return;
                    new android.app.AlertDialog.Builder(this).setTitle("Radio stations")
                        .setItems(names,(dialog,index) -> refresh("/station/"+ids[index],-1))
                        .setPositiveButton("World Radio…",(dialog,which) -> new WorldRadio(target).open())
                        .setNegativeButton("Close",null).show();
                });
            } catch (Exception ex) { ui.post(() -> {
                if (foreground && session==target && !isDestroyed())
                    Toast.makeText(this,"Couldn’t load stations. "+message(ex),Toast.LENGTH_LONG).show();
            }); }
        });
    }
    private class WorldRadio {
        final Session target;
        EditText filter;
        Spinner kind;
        Button prev, next;
        TextView summary;
        ArrayAdapter<String> names;
        final ArrayList<String> ids = new ArrayList<>();
        android.app.AlertDialog dialog;
        String criteria = "";
        int page, request;

        WorldRadio(Session paired) { target=paired; }

        void open() {
            LinearLayout box=new LinearLayout(RemoteActivity.this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding(dp(12),0,dp(12),0);
            kind=new Spinner(RemoteActivity.this);
            kind.setAdapter(new ArrayAdapter<>(RemoteActivity.this,android.R.layout.simple_spinner_dropdown_item,
                    new String[]{"Station name","Genre or tag","Country code (e.g. US)"}));
            box.addView(kind);
            filter=new EditText(RemoteActivity.this); filter.setSingleLine();
            filter.setHint("Leave blank for popular stations"); box.addView(filter);
            LinearLayout nav=new LinearLayout(RemoteActivity.this); box.addView(nav);
            button(nav,"Search",this::search);
            prev=button(nav,"Previous",() -> load(page-1));
            next=button(nav,"Next",() -> load(page+1));
            summary=label(box,"",14);
            ListView list=new ListView(RemoteActivity.this);
            box.addView(list,new LinearLayout.LayoutParams(-1,0,1));
            names=new ArrayAdapter<>(RemoteActivity.this,android.R.layout.simple_list_item_1,new ArrayList<>());
            list.setAdapter(names);
            list.setOnItemClickListener((parent,view,index,id) -> {
                String station=ids.get(index);
                dialog.dismiss(); refresh("/station/"+station,-1);
            });
            dialog=new android.app.AlertDialog.Builder(RemoteActivity.this).setTitle("World Radio")
                    .setView(box).setNegativeButton("Close",null).create();
            dialog.setOnDismissListener(d -> request++);
            dialog.show();
            dialog.getWindow().setLayout(-1,-1);
            dialog.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                    | android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            filter.setOnEditorActionListener((v,action,event) -> { search(); return true; });
            load(0);
        }

        void search() {
            String value=filter.getText().toString().trim();
            int selected=kind.getSelectedItemPosition();
            if (selected==2 && !value.isEmpty() && !value.matches("[a-zA-Z]{2}")) {
                filter.setError("Use a two-letter country code"); return;
            }
            criteria="&"+new String[]{"name","tag","country"}[selected]+"="+android.net.Uri.encode(value);
            android.view.inputmethod.InputMethodManager keyboard=(android.view.inputmethod.InputMethodManager)
                    getSystemService(INPUT_METHOD_SERVICE);
            keyboard.hideSoftInputFromWindow(filter.getWindowToken(),0);
            load(0);
        }

        void load(int newPage) {
            page=newPage; prev.setEnabled(false); next.setEnabled(false);
            ids.clear(); names.clear(); summary.setText("Searching Radio Browser…");
            fetch(++request,"/world?page="+page+criteria,true);
        }

        boolean current(int id) {
            return id==request && session==target && !isDestroyed() && dialog.isShowing();
        }

        void fetch(int id,String path,boolean start) {
            if (!current(id)) return;
            if (!foreground) { ui.postDelayed(() -> fetch(id,path,start),1000); return; }
            worker.execute(() -> {
                try {
                    JSONObject data=call(target,path,start?"POST":"GET",null,-1,-1);
                    ui.post(() -> {
                        if (!current(id)) return;
                        if (data.optBoolean("busy")) {
                            String pending="/world?generation="+data.optLong("generation");
                            ui.postDelayed(() -> fetch(id,pending,false),650); return;
                        }
                        JSONArray rows=data.optJSONArray("stations");
                        if (rows!=null) for (int i=0;i<rows.length();i++) {
                            JSONObject station=rows.optJSONObject(i);
                            ids.add(station.optString("id")); names.add(station.optString("name"));
                        }
                        prev.setEnabled(page>0); next.setEnabled(data.optBoolean("next"));
                        summary.setText(data.optString("status"));
                    });
                } catch (Exception ex) { ui.post(() -> {
                    if (current(id)) {
                        prev.setEnabled(page>0);
                        summary.setText(ex instanceof HttpError && ((HttpError)ex).status==409 ?
                                "Search changed on the player. Tap Search to reload." : "World Radio: "+message(ex));
                    }
                }); }
            });
        }
    }
    private void forget() {
        ui.removeCallbacks(poll); session=null; queueOffset=wantedOffset=0; revision=-1; busy=false;
        prefs.edit().remove("pin").remove("token").apply(); render();
    }
    @Override protected void onActivityResult(int request, int result, android.content.Intent data) {
        super.onActivityResult(request,result,data);
        if (request!=41 || session!=null || data==null) return;
        if (result!=RESULT_OK) {
            status.setText(data.getStringExtra("message"));
            return;
        }
        String[] fields=ScanActivity.parsePairing(data.getStringExtra("payload"));
        if (fields==null) { status.setText("Invalid Modulez pairing QR code."); return; }
        address.setText(fields[1]);
        code.setText(fields[2].substring(0,8)+"-"+fields[3]);
        pairWith(fields[1],fields[2],fields[3]);
    }

    private void pair() {
        String host = address.getText().toString().trim();
        String entered = code.getText().toString().toLowerCase(Locale.US).replaceAll("[-\\s]","");
        if (!android.util.Patterns.IP_ADDRESS.matcher(host).matches() || host.contains(":") ||
                !entered.matches("[0-9a-f]{14}")) {
            status.setText("Enter the player’s IPv4 address and the complete pairing code."); return;
        }
        pairWith(host,entered.substring(0,8),entered.substring(8));
    }

    private void pairWith(String host,String fingerprint,String secret) {
        Session candidate = new Session(host,fingerprint,"");
        scan.setEnabled(false);
        pair.setEnabled(false); status.setText("Pairing…");
        worker.execute(() -> {
            try {
                JSONObject reply = call(candidate,"/pair","POST",secret,-1,-1);
                Session paired = new Session(host,reply.getString("pin"),reply.getString("token"));
                ui.post(() -> {
                    if (isDestroyed()) return;
                    session=paired;
                    prefs.edit().putString("host",host).putString("pin",paired.pin).putString("token",paired.token).apply();
                    render(); refresh(null,-1);
                });
            } catch (Exception ex) { ui.post(() -> {
                if (!isDestroyed()) { scan.setEnabled(true); pair.setEnabled(true); status.setText("Pairing failed. Check Wi-Fi, address and code. " + message(ex)); }
            }); }
        });
    }
    private String message(Exception ex) {
        if (ex instanceof HttpError && ((HttpError)ex).status==409) return "The player is busy or the selection changed. Try again.";
        if (ex instanceof SSLException) return "Check the pairing code. Pair again if the player restarted.";
        if (ex instanceof HttpError && ((HttpError)ex).status==410) return "Start a new pairing session on the player.";
        return ex.getLocalizedMessage();
    }
    private void changeQueuePage(int delta) {
        wantedOffset=Math.max(0,queueOffset+delta);
        refresh(null,-1);
    }
    private void refresh(String action, int value) {
        if (session == null || !foreground || (action == null && busy)) return;
        ui.removeCallbacks(poll);
        final Session target=session; final long seen=revision; final int page=wantedOffset, displayed=queueOffset; busy=true;
        worker.execute(() -> {
            try {
                if (action != null) call(target,action,"POST",null,value,seen);
                JSONObject state=call(target,"/state","GET",null,-1,-1);
                JSONObject queue=state.optBoolean("importing") || (state.getLong("revision")==seen && page==displayed) ? null : call(target,"/playlist?offset="+page,"GET",null,-1,-1);
                ui.post(() -> {
                    if (session!=target || isDestroyed()) return;
                    title.setText(state.optString("title")); format.setText(state.optString("format"));
                    toggle.setText(state.optBoolean("playing") ? (state.optBoolean("radio") ? "Stop" : "Pause") : "Play");
                    if (!dragging) { volume.setProgress(state.optInt("volume")); volumeText.setText("Player volume  " + state.optInt("volume") + "%"); }
                    if (queue != null && page==wantedOffset) {
                        revision=queue.optLong("revision"); JSONArray rows=queue.optJSONArray("tracks");
                        tracks.setNotifyOnChange(false); tracks.clear();
                        for (int i=0;i<rows.length();i++) tracks.add(rows.optString(i));
                        tracks.notifyDataSetChanged();
                        queueOffset=wantedOffset=queue.optInt("offset");
                        int total=queue.optInt("total",rows.length());
                        queueNav.setVisibility(total>150?android.view.View.VISIBLE:android.view.View.GONE);
                        earlier.setEnabled(queueOffset>0);
                        later.setEnabled(queueOffset+rows.length()<total);
                        range.setText(" "+(queueOffset+1)+"–"+(queueOffset+rows.length())+" / "+total+" ");
                    }
                    String batch=state.optString("importStatus");
                    status.setText(batch.isEmpty()?"Connected to " + target.host:batch); schedule();
                });
            } catch (Exception ex) { ui.post(() -> {
                if (session!=target || isDestroyed()) return;
                status.setText("Connection issue. " + message(ex)); schedule();
            }); }
        });
    }
    private void schedule() {
        busy=false; ui.removeCallbacks(poll); if (foreground) ui.postDelayed(poll,1500);
    }
    @Override protected void onResume() { super.onResume(); foreground=true; refresh(null,-1); }
    @Override protected void onPause() { foreground=false; ui.removeCallbacks(poll); super.onPause(); }
    @Override protected void onDestroy() { ui.removeCallbacks(poll); worker.shutdownNow(); super.onDestroy(); }
}
