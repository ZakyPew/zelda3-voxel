using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Reflection;

namespace Zelda3VoxelLauncher;

internal sealed class LauncherForm : Form
{
    private static readonly Color Night = Color.FromArgb(5, 12, 24);
    private static readonly Color Card = Color.FromArgb(13, 25, 42);
    private static readonly Color Gold = Color.FromArgb(244, 196, 67);
    private static readonly Color Mint = Color.FromArgb(126, 214, 158);
    private static readonly Color Soft = Color.FromArgb(197, 210, 224);
    private readonly string gameDirectory = AppContext.BaseDirectory;
    private readonly CheckBox voxel = MakeCheck("Enable voxel diorama", true);
    private readonly ComboBox cameraMode = new();
    private readonly CheckBox flatHud = MakeCheck("Keep only HUD elements flat", true);
    private readonly CheckBox filtering = MakeCheck("Smooth texture filtering", false);
    private readonly ComboBox voxelSize = new();
    private readonly TrackBar voxelHeight = new();
    private readonly Label heightValue = MakeLabel("55%", 10, Gold);
    private readonly TrackBar voxelPitch = new();
    private readonly Label pitchValue = MakeLabel("39°", 10, Gold);
    private readonly TrackBar voxelZoom = new();
    private readonly Label zoomValue = MakeLabel("100%", 10, Gold);
    private readonly ComboBox fullscreen = new();
    private readonly NumericUpDown windowScale = new();
    private readonly ComboBox outputMethod = new();
    private readonly TextBox windowSize = new();
    private readonly CheckBox enhancedMode7 = MakeCheck("High-quality world map (Mode 7)", true);
    private readonly CheckBox newRenderer = MakeCheck("Use enhanced renderer", true);
    private readonly CheckBox ignoreAspectRatio = MakeCheck("Ignore aspect ratio", false);
    private readonly CheckBox noSpriteLimits = MakeCheck("Remove sprite limits", false);
    private readonly CheckBox dimFlashes = MakeCheck("Dim bright flashes", false);
    private readonly CheckBox displayPerf = MakeCheck("Show performance in title", false);
    private readonly CheckBox disableFrameDelay = MakeCheck("Disable frame delay", false);
    private readonly TextBox shader = new();
    private readonly TextBox linkGraphics = new();
    private readonly CheckBox autosave = MakeCheck("Enable autosave", false);
    private readonly ComboBox aspectRatio = new();
    private readonly ComboBox language = new();
    private readonly CheckBox enableAudio = MakeCheck("Enable audio", true);
    private readonly NumericUpDown audioFreq = new();
    private readonly NumericUpDown audioChannels = new();
    private readonly NumericUpDown audioSamples = new();
    private readonly ComboBox msuMode = new();
    private readonly TextBox msuPath = new();
    private readonly NumericUpDown msuVolume = new();
    private readonly CheckBox resumeMsu = MakeCheck("Resume MSU audio", false);
    private readonly CheckBox itemSwitch = MakeCheck("Switch items with L/R", false);
    private readonly CheckBox itemSwitchLimit = MakeCheck("Limit L/R item switching", false);
    private readonly CheckBox turnWhileDashing = MakeCheck("Turn while dashing", false);
    private readonly CheckBox mirrorDarkworld = MakeCheck("Mirror to Dark World", false);
    private readonly CheckBox collectSword = MakeCheck("Collect items with sword", false);
    private readonly CheckBox breakPots = MakeCheck("Break pots with sword", false);
    private readonly CheckBox disableLowHealth = MakeCheck("Disable low-health beep", false);
    private readonly CheckBox skipIntro = MakeCheck("Skip intro on keypress", false);
    private readonly CheckBox showMaxItems = MakeCheck("Show max items in yellow", false);
    private readonly CheckBox moreBombs = MakeCheck("Allow more active bombs", false);
    private readonly CheckBox moreRupees = MakeCheck("Carry more rupees", false);
    private readonly CheckBox miscBugFixes = MakeCheck("Enable miscellaneous bug fixes", false);
    private readonly CheckBox gameChangingBugFixes = MakeCheck("Enable game-changing bug fixes", false);
    private readonly CheckBox cancelBirdTravel = MakeCheck("Allow cancelling bird travel", false);
    private readonly TextBox keyMap = new();
    private readonly TextBox gamepadMap = new();
    private readonly Label status = MakeLabel(string.Empty, 9, Soft);
    private readonly Panel homePage = new() { Dock = DockStyle.Fill, BackColor = Color.Transparent };
    private readonly Panel settingsPage = new() { Dock = DockStyle.Fill, BackColor = Night, Visible = false };

    public LauncherForm()
    {
        Text = "Zelda3 Voxel Launcher";
        ClientSize = new Size(1120, 650);
        MinimumSize = new Size(980, 600);
        BackColor = Night; ForeColor = Color.White; Font = new Font("Segoe UI", 10f);
        StartPosition = FormStartPosition.CenterScreen; DoubleBuffered = true;
        var hero = new HeroPanel(LoadHeroImage()) { Dock = DockStyle.Fill };
        Controls.Add(hero); hero.Controls.Add(settingsPage); hero.Controls.Add(homePage);
        BuildHomePage(); BuildSettingsPage(); LoadSettings(); RefreshStatus();
    }

    private static Image? LoadHeroImage()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("Zelda3VoxelLauncher.assets.zelda3-voxel-launcher-hero.png");
        return stream == null ? null : new Bitmap(Image.FromStream(stream));
    }

    private void BuildHomePage()
    {
        var left = new Panel { Width = 420, Dock = DockStyle.Left, BackColor = Color.Transparent };
        homePage.Controls.Add(left);
        var eyebrow = MakeLabel("A LINK TO THE PAST", 10, Mint, FontStyle.Bold); eyebrow.SetBounds(61, 42, 310, 26);
        var title = MakeLabel("ZELDA3", 36, Color.White, FontStyle.Bold, "Georgia"); title.SetBounds(55, 67, 330, 55);
        var subtitle = MakeLabel("VOXEL", 20, Gold, FontStyle.Regular, "Georgia"); subtitle.SetBounds(59, 120, 240, 36);
        var rule = new Panel { BackColor = Gold }; rule.SetBounds(61, 170, 62, 2);
        var start = MakeMenuButton("START GAME"); var settings = MakeMenuButton("SETTINGS");
        var folder = MakeMenuButton("OPEN GAME FOLDER"); var exit = MakeMenuButton("EXIT");
        start.SetBounds(57, 250, 280, 46); settings.SetBounds(57, 300, 280, 46);
        folder.SetBounds(57, 350, 280, 46); exit.SetBounds(57, 400, 280, 46);
        start.Click += (_, _) => PlayGame(); settings.Click += (_, _) => ShowSettings();
        folder.Click += (_, _) => Process.Start(new ProcessStartInfo("explorer.exe", gameDirectory) { UseShellExecute = true });
        exit.Click += (_, _) => Close();
        status.SetBounds(61, 525, 320, 42);
        var version = MakeLabel("VOXEL ALPHA 0.2  •  PRESS 3 IN-GAME TO TOGGLE", 8, Color.FromArgb(145, 166, 184));
        version.SetBounds(61, 580, 340, 26);
        left.Controls.AddRange([eyebrow, title, subtitle, rule, start, settings, folder, exit, status, version]);
    }

    private void BuildSettingsPage()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 3, Padding = new Padding(42), BackColor = Night };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 72)); root.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        settingsPage.Controls.Add(root);
        var heading = new Panel { Dock = DockStyle.Fill, BackColor = Night };
        var title = MakeLabel("SETTINGS", 27, Color.White, FontStyle.Bold, "Georgia"); title.Dock = DockStyle.Top; title.Height = 42;
        var copy = MakeLabel("Configure the diorama and the original Zelda3 runtime.", 10, Soft); copy.Dock = DockStyle.Top;
        heading.Controls.Add(copy); heading.Controls.Add(title); root.Controls.Add(heading, 0, 0);

        var tabs = new TabControl { Dock = DockStyle.Fill, Appearance = TabAppearance.FlatButtons, ItemSize = new Size(132, 30), Padding = new Point(16, 5) };
        tabs.TabPages.Add(BuildDioramaTab()); tabs.TabPages.Add(BuildGraphicsTab()); tabs.TabPages.Add(BuildSoundTab());
        tabs.TabPages.Add(BuildGameplayTab()); tabs.TabPages.Add(BuildInputTab());
        root.Controls.Add(tabs, 0, 1);
        var actions = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, BackColor = Night };
        actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25)); actions.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        var back = MakeActionButton("BACK", Color.FromArgb(30, 45, 62), Color.White); var save = MakeActionButton("SAVE SETTINGS", Gold, Night);
        back.Click += (_, _) => ShowHome(); save.Click += (_, _) => { SaveSettings(); ShowHome(); };
        actions.Controls.Add(new Panel(), 0, 0); actions.Controls.Add(back, 1, 0); actions.Controls.Add(save, 2, 0); root.Controls.Add(actions, 0, 2);
    }

    private TabPage BuildDioramaTab()
    {
        var tab = MakeSettingsTab("Diorama");
        AddCheck(tab, voxel);
        AddControl(tab, MakeFieldLabel("Camera (press 4 in-game to cycle)"));
        cameraMode.DropDownStyle = ComboBoxStyle.DropDownList; cameraMode.Items.AddRange(["Diorama", "Chase — over the shoulder", "First person", "Orbit — free camera (Q/E rotate, +/- zoom)"]); StyleCombo(cameraMode); AddControl(tab, cameraMode);
        AddCheck(tab, flatHud); AddCheck(tab, MakeFieldLabel("Voxel block size"));
        voxelSize.DropDownStyle = ComboBoxStyle.DropDownList; voxelSize.Items.AddRange(["2 — Fine", "3 — Detailed", "4 — Balanced", "6 — Chunky", "8 — Extra chunky"]); StyleCombo(voxelSize); AddControl(tab, voxelSize);
        AddControl(tab, MakeFieldLabel("Extrusion height"));
        AddControl(tab, MakeSliderRow(voxelHeight, heightValue, 5, 100, 10, v => $"{v}%"));
        AddControl(tab, MakeFieldLabel("Camera pitch (degrees of chase tilt)"));
        AddControl(tab, MakeSliderRow(voxelPitch, pitchValue, 10, 80, 5, v => $"{v}°"));
        AddControl(tab, MakeFieldLabel("Camera zoom"));
        AddControl(tab, MakeSliderRow(voxelZoom, zoomValue, 50, 200, 10, v => $"{v}%"));
        return tab;
    }

    private static TableLayoutPanel MakeSliderRow(TrackBar bar, Label value, int min, int max, int tick, Func<int, string> format)
    {
        var row = new TableLayoutPanel { Width = 520, Height = 48, ColumnCount = 2, BackColor = Card };
        row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 84)); row.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 16));
        bar.Minimum = min; bar.Maximum = max; bar.TickFrequency = tick; bar.Dock = DockStyle.Fill;
        bar.ValueChanged += (_, _) => value.Text = format(bar.Value); value.TextAlign = ContentAlignment.MiddleRight;
        row.Controls.Add(bar, 0, 0); row.Controls.Add(value, 1, 0); return row;
    }

    private TabPage BuildGraphicsTab()
    {
        var tab = MakeSettingsTab("Graphics");
        AddControl(tab, MakeFieldLabel("Output renderer")); outputMethod.Items.AddRange(["OpenGL", "SDL", "SDL-Software", "OpenGL ES"]); outputMethod.DropDownStyle = ComboBoxStyle.DropDownList; StyleCombo(outputMethod); AddControl(tab, outputMethod);
        AddControl(tab, MakeFieldLabel("Window size (for example 1280x720 or Auto)")); windowSize.Width = 520; StyleText(windowSize); AddControl(tab, windowSize);
        AddControl(tab, MakeFieldLabel("Window mode")); fullscreen.Items.AddRange(["Windowed", "Borderless fullscreen", "Exclusive fullscreen"]); fullscreen.DropDownStyle = ComboBoxStyle.DropDownList; StyleCombo(fullscreen); AddControl(tab, fullscreen);
        AddControl(tab, MakeFieldLabel("Window scale (1-8)")); windowScale.Minimum = 1; windowScale.Maximum = 8; windowScale.Width = 520; StyleNumeric(windowScale); AddControl(tab, windowScale);
        AddControl(tab, MakeFieldLabel("Widescreen aspect ratio")); aspectRatio.Items.AddRange(["4:3", "16:9", "16:10", "18:9", "16:9 + extended vertical"]); aspectRatio.DropDownStyle = ComboBoxStyle.DropDownList; StyleCombo(aspectRatio); AddControl(tab, aspectRatio);
        AddCheck(tab, newRenderer); AddCheck(tab, enhancedMode7); AddCheck(tab, ignoreAspectRatio); AddCheck(tab, filtering); AddCheck(tab, noSpriteLimits); AddCheck(tab, dimFlashes); AddCheck(tab, displayPerf); AddCheck(tab, disableFrameDelay);
        AddControl(tab, MakeFieldLabel("Shader file (optional)")); shader.Width = 520; StyleText(shader); AddControl(tab, shader);
        AddControl(tab, MakeFieldLabel("Link graphics file (optional)")); linkGraphics.Width = 520; StyleText(linkGraphics); AddControl(tab, linkGraphics);
        return tab;
    }

    private TabPage BuildSoundTab()
    {
        var tab = MakeSettingsTab("Sound");
        AddCheck(tab, enableAudio);
        AddControl(tab, MakeFieldLabel("Audio frequency")); ConfigureNumeric(audioFreq, 8000, 192000, 32000); AddControl(tab, audioFreq);
        AddControl(tab, MakeFieldLabel("Audio channels")); ConfigureNumeric(audioChannels, 1, 8, 2); AddControl(tab, audioChannels);
        AddControl(tab, MakeFieldLabel("Audio samples")); ConfigureNumeric(audioSamples, 128, 4096, 1024); AddControl(tab, audioSamples);
        AddControl(tab, MakeFieldLabel("MSU audio mode")); msuMode.Items.AddRange(["Disabled", "MSU", "MSU Deluxe", "Opuz", "MSU Deluxe + Opuz"]); msuMode.DropDownStyle = ComboBoxStyle.DropDownList; StyleCombo(msuMode); AddControl(tab, msuMode);
        AddControl(tab, MakeFieldLabel("MSU path")); msuPath.Width = 520; StyleText(msuPath); AddControl(tab, msuPath);
        AddControl(tab, MakeFieldLabel("MSU volume (0-100)")); ConfigureNumeric(msuVolume, 0, 100, 100); AddControl(tab, msuVolume); AddCheck(tab, resumeMsu);
        return tab;
    }

    private TabPage BuildGameplayTab()
    {
        var tab = MakeSettingsTab("Gameplay");
        AddCheck(tab, autosave);
        AddControl(tab, MakeFieldLabel("Language")); language.Items.AddRange(["English", "Japanese", "French", "German", "Spanish", "Italian"]); language.DropDownStyle = ComboBoxStyle.DropDownList; StyleCombo(language); AddControl(tab, language);
        AddCheck(tab, itemSwitch); AddCheck(tab, itemSwitchLimit); AddCheck(tab, turnWhileDashing); AddCheck(tab, mirrorDarkworld); AddCheck(tab, collectSword); AddCheck(tab, breakPots); AddCheck(tab, disableLowHealth); AddCheck(tab, skipIntro); AddCheck(tab, showMaxItems); AddCheck(tab, moreBombs); AddCheck(tab, moreRupees); AddCheck(tab, miscBugFixes); AddCheck(tab, gameChangingBugFixes); AddCheck(tab, cancelBirdTravel);
        return tab;
    }

    private TabPage BuildInputTab()
    {
        var tab = MakeSettingsTab("Input");
        AddControl(tab, MakeFieldLabel("Keyboard bindings (comma-separated; see Zelda3 key names)")); keyMap.Multiline = true; keyMap.ScrollBars = ScrollBars.Vertical; keyMap.Width = 520; keyMap.Height = 92; StyleText(keyMap); AddControl(tab, keyMap);
        AddControl(tab, MakeFieldLabel("Gamepad bindings (comma-separated)")); gamepadMap.Multiline = true; gamepadMap.ScrollBars = ScrollBars.Vertical; gamepadMap.Width = 520; gamepadMap.Height = 92; StyleText(gamepadMap); AddControl(tab, gamepadMap);
        var note = MakeLabel("Leave either field blank to use Zelda3 defaults. Advanced bindings are written directly to the original [KeyMap] and [GamepadMap] sections.", 9, Soft); note.Width = 520; note.Height = 46; AddControl(tab, note);
        return tab;
    }

    private static TabPage MakeSettingsTab(string title)
    {
        var tab = new TabPage(title) { BackColor = Night, ForeColor = Color.White, Padding = new Padding(10) };
        var flow = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true, BackColor = Night };
        tab.Controls.Add(flow); return tab;
    }

    private static void AddControl(TabPage tab, Control control) => ((FlowLayoutPanel)tab.Controls[0]).Controls.Add(control);
    private static void AddCheck(TabPage tab, Control control) => AddControl(tab, control);
    private static void StyleText(TextBox box) { box.BackColor = Color.FromArgb(18, 31, 48); box.ForeColor = Color.White; box.BorderStyle = BorderStyle.FixedSingle; }
    private static void StyleNumeric(NumericUpDown control) { control.BackColor = Night; control.ForeColor = Color.White; }
    private static void ConfigureNumeric(NumericUpDown control, decimal min, decimal max, decimal value) { control.Minimum = min; control.Maximum = max; control.Value = value; control.Width = 520; StyleNumeric(control); }
    private static void ConfigureValue(NumericUpDown control, string text, decimal min, decimal max, decimal fallback)
    {
        control.Minimum = min; control.Maximum = max; control.Value = decimal.TryParse(text, out var value) ? Math.Clamp(value, min, max) : fallback;
    }

    private Panel MakeCard(string heading)
    {
        var card = new Panel { Dock = DockStyle.Fill, Margin = new Padding(7), Padding = new Padding(26), BackColor = Card };
        card.Paint += (_, e) => { using var pen = new Pen(Color.FromArgb(45, 66, 82)); e.Graphics.DrawRectangle(pen, 0, 0, card.Width - 1, card.Height - 1); };
        var flow = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false, BackColor = Card };
        var title = MakeLabel(heading, 12, Gold, FontStyle.Bold); title.Width = 340; title.Height = 34;
        flow.Controls.Add(title); card.Controls.Add(flow); return card;
    }

    private void ShowSettings() { homePage.Visible = false; settingsPage.Visible = true; settingsPage.BringToFront(); }
    private void ShowHome() { settingsPage.Visible = false; homePage.Visible = true; homePage.BringToFront(); RefreshStatus(); }

    private void LoadSettings()
    {
        var ini = new IniFile(Path.Combine(gameDirectory, "zelda3.ini"));
        outputMethod.SelectedIndex = ini.Get("Graphics", "OutputMethod", "OpenGL").ToUpperInvariant() switch { "SDL" => 1, "SDL-SOFTWARE" => 2, "OPENGL ES" => 3, _ => 0 };
        windowSize.Text = ini.Get("Graphics", "WindowSize", "Auto");
        windowSize.Text = string.IsNullOrWhiteSpace(windowSize.Text) ? "Auto" : windowSize.Text;
        enhancedMode7.Checked = ReadBool(ini.Get("Graphics", "EnhancedMode7", "true"));
        newRenderer.Checked = ReadBool(ini.Get("Graphics", "NewRenderer", "true"));
        ignoreAspectRatio.Checked = ReadBool(ini.Get("Graphics", "IgnoreAspectRatio", "false"));
        filtering.Checked = ReadBool(ini.Get("Graphics", "LinearFiltering", "false"));
        noSpriteLimits.Checked = ReadBool(ini.Get("Graphics", "NoSpriteLimits", "false"));
        dimFlashes.Checked = ReadBool(ini.Get("Graphics", "DimFlashes", "false"));
        displayPerf.Checked = ReadBool(ini.Get("General", "DisplayPerfInTitle", "false"));
        disableFrameDelay.Checked = ReadBool(ini.Get("General", "DisableFrameDelay", "false"));
        shader.Text = ini.Get("Graphics", "Shader", "");
        linkGraphics.Text = ini.Get("Graphics", "LinkGraphics", "");
        voxel.Checked = ReadBool(ini.Get("Graphics", "VoxelMode", "true")); flatHud.Checked = !ReadBool(ini.Get("Graphics", "VoxelizeHud", "false"));
        var camDefault = ReadBool(ini.Get("Graphics", "VoxelChaseCam", "false")) ? 1 : 0;
        cameraMode.SelectedIndex = Math.Clamp(int.TryParse(ini.Get("Graphics", "VoxelCamera", camDefault.ToString()), out var cm) ? cm : camDefault, 0, 3);
        var size = int.TryParse(ini.Get("Graphics", "VoxelSize", "4"), out var parsedSize) ? parsedSize : 4;
        voxelSize.SelectedIndex = size switch { 2 => 0, 3 => 1, 6 => 3, 8 => 4, _ => 2 };
        voxelHeight.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "VoxelHeight", "55"), out var h) ? h : 55, 5, 100);
        voxelPitch.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "VoxelPitch", "39"), out var p) ? p : 39, 10, 80);
        voxelZoom.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "VoxelZoom", "100"), out var z) ? z : 100, 50, 200);
        fullscreen.SelectedIndex = Math.Clamp(int.TryParse(ini.Get("Graphics", "Fullscreen", "0"), out var f) ? f : 0, 0, 2);
        windowScale.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "WindowScale", "2"), out var s) ? s : 2, 1, 8);
        enableAudio.Checked = ReadBool(ini.Get("Sound", "EnableAudio", "true"));
        ConfigureValue(audioFreq, ini.Get("Sound", "AudioFreq", "44100"), 8000, 192000, 44100);
        ConfigureValue(audioChannels, ini.Get("Sound", "AudioChannels", "2"), 1, 8, 2);
        ConfigureValue(audioSamples, ini.Get("Sound", "AudioSamples", "1024"), 128, 4096, 1024);
        msuMode.SelectedIndex = ini.Get("Sound", "EnableMSU", "0").ToLowerInvariant() switch { "msu" or "1" => 1, "deluxe" or "2" => 2, "opuz" or "4" => 3, "deluxe-opuz" or "6" => 4, _ => 0 };
        msuPath.Text = ini.Get("Sound", "MSUPath", "");
        ConfigureValue(msuVolume, ini.Get("Sound", "MSUVolume", "100"), 0, 100, 100);
        resumeMsu.Checked = ReadBool(ini.Get("Sound", "ResumeMSU", "false"));
        autosave.Checked = ReadBool(ini.Get("General", "Autosave", "false"));
        var ratio = ini.Get("General", "ExtendedAspectRatio", "4:3").ToLowerInvariant();
        aspectRatio.SelectedIndex = ratio.Contains("extend_y") ? 4 : ratio.Contains("16:10") ? 2 : ratio.Contains("18:9") ? 3 : ratio.Contains("16:9") ? 1 : 0;
        language.SelectedIndex = ini.Get("General", "Language", "English").ToLowerInvariant() switch { "japanese" or "jp" => 1, "french" => 2, "german" => 3, "spanish" => 4, "italian" => 5, _ => 0 };
        itemSwitch.Checked = ReadBool(ini.Get("Features", "ItemSwitchLR", "false"));
        itemSwitchLimit.Checked = ReadBool(ini.Get("Features", "ItemSwitchLRLimit", "false"));
        turnWhileDashing.Checked = ReadBool(ini.Get("Features", "TurnWhileDashing", "false"));
        mirrorDarkworld.Checked = ReadBool(ini.Get("Features", "MirrorToDarkworld", "false"));
        collectSword.Checked = ReadBool(ini.Get("Features", "CollectItemsWithSword", "false"));
        breakPots.Checked = ReadBool(ini.Get("Features", "BreakPotsWithSword", "false"));
        disableLowHealth.Checked = ReadBool(ini.Get("Features", "DisableLowHealthBeep", "false"));
        skipIntro.Checked = ReadBool(ini.Get("Features", "SkipIntroOnKeypress", "false"));
        showMaxItems.Checked = ReadBool(ini.Get("Features", "ShowMaxItemsInYellow", "false"));
        moreBombs.Checked = ReadBool(ini.Get("Features", "MoreActiveBombs", "false"));
        moreRupees.Checked = ReadBool(ini.Get("Features", "CarryMoreRupees", "false"));
        miscBugFixes.Checked = ReadBool(ini.Get("Features", "MiscBugFixes", "false"));
        gameChangingBugFixes.Checked = ReadBool(ini.Get("Features", "GameChangingBugFixes", "false"));
        cancelBirdTravel.Checked = ReadBool(ini.Get("Features", "CancelBirdTravel", "false"));
        keyMap.Text = ini.Get("KeyMap", "Controls", "");
        gamepadMap.Text = ini.Get("GamepadMap", "Controls", "");
    }

    private void SaveSettings()
    {
        var ini = new IniFile(Path.Combine(gameDirectory, "zelda3.ini")); var sizes = new[] { 2, 3, 4, 6, 8 };
        ini.Set("Graphics", "OutputMethod", new[] { "OpenGL", "SDL", "SDL-Software", "OpenGL ES" }[Math.Max(0, outputMethod.SelectedIndex)]);
        ini.Set("Graphics", "WindowSize", string.IsNullOrWhiteSpace(windowSize.Text) ? "Auto" : windowSize.Text.Trim());
        ini.Set("Graphics", "EnhancedMode7", Bool(enhancedMode7.Checked)); ini.Set("Graphics", "NewRenderer", Bool(newRenderer.Checked));
        ini.Set("Graphics", "IgnoreAspectRatio", Bool(ignoreAspectRatio.Checked)); ini.Set("Graphics", "Fullscreen", fullscreen.SelectedIndex.ToString());
        ini.Set("Graphics", "WindowScale", windowScale.Value.ToString()); ini.Set("Graphics", "LinearFiltering", Bool(filtering.Checked)); ini.Set("Graphics", "NoSpriteLimits", Bool(noSpriteLimits.Checked));
        ini.Set("Graphics", "VoxelMode", Bool(voxel.Checked)); ini.Set("Graphics", "VoxelCamera", Math.Max(0, cameraMode.SelectedIndex).ToString()); ini.Set("Graphics", "VoxelizeHud", Bool(!flatHud.Checked)); ini.Set("Graphics", "VoxelSize", sizes[Math.Max(0, voxelSize.SelectedIndex)].ToString());
        ini.Set("Graphics", "VoxelHeight", voxelHeight.Value.ToString()); ini.Set("Graphics", "VoxelPitch", voxelPitch.Value.ToString()); ini.Set("Graphics", "VoxelZoom", voxelZoom.Value.ToString());
        if (string.IsNullOrWhiteSpace(shader.Text)) ini.Remove("Graphics", "Shader"); else ini.Set("Graphics", "Shader", shader.Text.Trim());
        if (string.IsNullOrWhiteSpace(linkGraphics.Text)) ini.Remove("Graphics", "LinkGraphics"); else ini.Set("Graphics", "LinkGraphics", linkGraphics.Text.Trim());
        ini.Set("Graphics", "DimFlashes", Bool(dimFlashes.Checked)); ini.Set("General", "Autosave", Bool(autosave.Checked)); ini.Set("General", "DisplayPerfInTitle", Bool(displayPerf.Checked)); ini.Set("General", "DisableFrameDelay", Bool(disableFrameDelay.Checked));
        ini.Set("General", "ExtendedAspectRatio", new[] { "4:3", "16:9", "16:10", "18:9", "extend_y, 16:9" }[Math.Max(0, aspectRatio.SelectedIndex)]);
        ini.Set("General", "Language", new[] { "English", "Japanese", "French", "German", "Spanish", "Italian" }[Math.Max(0, language.SelectedIndex)]);
        ini.Set("Sound", "EnableAudio", Bool(enableAudio.Checked)); ini.Set("Sound", "AudioFreq", audioFreq.Value.ToString()); ini.Set("Sound", "AudioChannels", audioChannels.Value.ToString()); ini.Set("Sound", "AudioSamples", audioSamples.Value.ToString());
        ini.Set("Sound", "EnableMSU", new[] { "0", "msu", "deluxe", "opuz", "deluxe-opuz" }[Math.Max(0, msuMode.SelectedIndex)]); ini.Set("Sound", "MSUPath", msuPath.Text.Trim()); ini.Set("Sound", "MSUVolume", msuVolume.Value.ToString()); ini.Set("Sound", "ResumeMSU", Bool(resumeMsu.Checked));
        ini.Set("Features", "ItemSwitchLR", Bool(itemSwitch.Checked)); ini.Set("Features", "ItemSwitchLRLimit", Bool(itemSwitchLimit.Checked)); ini.Set("Features", "TurnWhileDashing", Bool(turnWhileDashing.Checked)); ini.Set("Features", "MirrorToDarkworld", Bool(mirrorDarkworld.Checked));
        ini.Set("Features", "CollectItemsWithSword", Bool(collectSword.Checked)); ini.Set("Features", "BreakPotsWithSword", Bool(breakPots.Checked)); ini.Set("Features", "DisableLowHealthBeep", Bool(disableLowHealth.Checked)); ini.Set("Features", "SkipIntroOnKeypress", Bool(skipIntro.Checked)); ini.Set("Features", "ShowMaxItemsInYellow", Bool(showMaxItems.Checked));
        ini.Set("Features", "MoreActiveBombs", Bool(moreBombs.Checked)); ini.Set("Features", "CarryMoreRupees", Bool(moreRupees.Checked)); ini.Set("Features", "MiscBugFixes", Bool(miscBugFixes.Checked)); ini.Set("Features", "GameChangingBugFixes", Bool(gameChangingBugFixes.Checked)); ini.Set("Features", "CancelBirdTravel", Bool(cancelBirdTravel.Checked));
        if (!string.IsNullOrWhiteSpace(keyMap.Text)) ini.Set("KeyMap", "Controls", keyMap.Text.Trim());
        if (!string.IsNullOrWhiteSpace(gamepadMap.Text)) ini.Set("GamepadMap", "Controls", gamepadMap.Text.Trim());
        ini.Save();
    }

    private void PlayGame()
    {
        var game = Path.Combine(gameDirectory, "zelda3.exe"); var assets = Path.Combine(gameDirectory, "zelda3_assets.dat");
        if (!File.Exists(game) || !File.Exists(assets)) { MessageBox.Show("The launcher must sit beside zelda3.exe and zelda3_assets.dat.", "Game files missing", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
        try
        {
            SaveSettings();
            var process = Process.Start(new ProcessStartInfo(game) { WorkingDirectory = gameDirectory, UseShellExecute = true });
            if (process == null) throw new InvalidOperationException("Windows did not return a game process.");
            status.Text = "GAME STARTING"; status.ForeColor = Gold;
            Hide();
            BeginInvoke(async () =>
            {
                await Task.Delay(750);
                if (process.HasExited)
                {
                    Show(); WindowState = FormWindowState.Normal; Activate();
                    status.Text = $"GAME EXITED  •  CODE {process.ExitCode}"; status.ForeColor = Color.FromArgb(244, 151, 123);
                    MessageBox.Show($"Zelda3 Voxel closed during startup (exit code {process.ExitCode}). Check zelda3.ini and the local asset pack.", "Game startup failed", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            });
        }
        catch (Exception ex)
        {
            Show(); WindowState = FormWindowState.Normal; Activate();
            status.Text = "LAUNCH FAILED"; status.ForeColor = Color.FromArgb(244, 151, 123);
            MessageBox.Show($"Unable to start Zelda3 Voxel:\n\n{ex.Message}", "Launch failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void RefreshStatus()
    {
        var ok = File.Exists(Path.Combine(gameDirectory, "zelda3.exe")) && File.Exists(Path.Combine(gameDirectory, "zelda3_assets.dat"));
        status.Text = ok ? "READY  •  LOCAL ASSET PACK FOUND" : "SETUP NEEDED  •  GAME FILES MISSING"; status.ForeColor = ok ? Mint : Color.FromArgb(244, 151, 123);
    }

    private static Button MakeMenuButton(string text)
    {
        var button = new Button { Text = "  " + text, TextAlign = ContentAlignment.MiddleLeft, BackColor = Color.Transparent, ForeColor = Color.White, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand, Font = new Font("Georgia", 12), TabStop = true };
        button.FlatAppearance.BorderSize = 0; button.FlatAppearance.MouseOverBackColor = Color.FromArgb(35, 53, 66); button.FlatAppearance.MouseDownBackColor = Color.FromArgb(55, 70, 69);
        button.MouseEnter += (_, _) => button.ForeColor = Gold; button.MouseLeave += (_, _) => button.ForeColor = Color.White; return button;
    }
    private static Button MakeActionButton(string text, Color back, Color fore) => new() { Text = text, Dock = DockStyle.Fill, Margin = new Padding(7), BackColor = back, ForeColor = fore, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand, Font = new Font("Segoe UI Semibold", 10, FontStyle.Bold) };
    private static CheckBox MakeCheck(string text, bool value) => new() { Text = text, Checked = value, Width = 340, Height = 36, ForeColor = Color.White, BackColor = Card, FlatStyle = FlatStyle.Flat };
    private static Label MakeFieldLabel(string text) => new() { Text = text.ToUpperInvariant(), Width = 340, Height = 26, Margin = new Padding(3, 12, 3, 0), ForeColor = Soft, BackColor = Card, Font = new Font("Segoe UI Semibold", 8) };
    private static Label MakeLabel(string text, float size, Color color, FontStyle style = FontStyle.Regular, string family = "Segoe UI") => new() { Text = text, AutoSize = false, ForeColor = color, BackColor = Color.Transparent, Font = new Font(family, size, style) };
    private static void StyleCombo(ComboBox combo) { combo.Width = 340; combo.Height = 34; combo.BackColor = Night; combo.ForeColor = Color.White; combo.FlatStyle = FlatStyle.Flat; }
    private static bool ReadBool(string value) => value.Equals("true", StringComparison.OrdinalIgnoreCase) || value is "1" or "yes" or "on";
    private static string Bool(bool value) => value ? "true" : "false";
}

internal sealed class HeroPanel : Panel
{
    private readonly Image? hero;
    public HeroPanel(Image? hero) { this.hero = hero; DoubleBuffered = true; ResizeRedraw = true; }
    protected override void OnPaintBackground(PaintEventArgs e)
    {
        e.Graphics.Clear(Color.FromArgb(5, 12, 24)); if (hero == null) return;
        var scale = Math.Max((float)Width / hero.Width, (float)Height / hero.Height); var w = (int)(hero.Width * scale); var h = (int)(hero.Height * scale);
        e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic; e.Graphics.DrawImage(hero, (Width - w) / 2, (Height - h) / 2, w, h);
        using var shade = new LinearGradientBrush(ClientRectangle, Color.FromArgb(238, 3, 10, 22), Color.FromArgb(8, 3, 10, 22), LinearGradientMode.Horizontal);
        e.Graphics.FillRectangle(shade, ClientRectangle);
    }
}
