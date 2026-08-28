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
    private readonly CheckBox flatHud = MakeCheck("Keep only HUD elements flat", true);
    private readonly CheckBox filtering = MakeCheck("Smooth texture filtering", false);
    private readonly CheckBox audio = MakeCheck("Enable audio", true);
    private readonly ComboBox voxelSize = new();
    private readonly TrackBar voxelHeight = new();
    private readonly Label heightValue = MakeLabel("55%", 10, Gold);
    private readonly ComboBox fullscreen = new();
    private readonly NumericUpDown windowScale = new();
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
        var version = MakeLabel("VOXEL SLICE 0.1  •  PRESS 3 IN-GAME TO TOGGLE", 8, Color.FromArgb(145, 166, 184));
        version.SetBounds(61, 580, 340, 26);
        left.Controls.AddRange([eyebrow, title, subtitle, rule, start, settings, folder, exit, status, version]);
    }

    private void BuildSettingsPage()
    {
        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, RowCount = 3, Padding = new Padding(52), BackColor = Night };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50)); root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 100)); root.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 72));
        settingsPage.Controls.Add(root);
        var heading = new Panel { Dock = DockStyle.Fill, BackColor = Night };
        var title = MakeLabel("SETTINGS", 27, Color.White, FontStyle.Bold, "Georgia"); title.Dock = DockStyle.Top; title.Height = 50;
        var copy = MakeLabel("Tune the diorama and original runtime.", 10, Soft); copy.Dock = DockStyle.Top;
        heading.Controls.Add(copy); heading.Controls.Add(title); root.Controls.Add(heading, 0, 0); root.SetColumnSpan(heading, 3);

        var voxelCard = MakeCard("DIORAMA"); var vf = (FlowLayoutPanel)voxelCard.Controls[0];
        vf.Controls.Add(voxel); vf.Controls.Add(flatHud); vf.Controls.Add(MakeFieldLabel("Voxel block size"));
        voxelSize.DropDownStyle = ComboBoxStyle.DropDownList; voxelSize.Items.AddRange(["2 — Fine", "3 — Detailed", "4 — Balanced", "6 — Chunky", "8 — Extra chunky"]);
        StyleCombo(voxelSize); vf.Controls.Add(voxelSize); vf.Controls.Add(MakeFieldLabel("Extrusion height"));
        var heightRow = new TableLayoutPanel { Width = 340, Height = 48, ColumnCount = 2, BackColor = Card };
        heightRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 84)); heightRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 16));
        voxelHeight.Minimum = 5; voxelHeight.Maximum = 100; voxelHeight.TickFrequency = 10; voxelHeight.Dock = DockStyle.Fill;
        voxelHeight.ValueChanged += (_, _) => heightValue.Text = $"{voxelHeight.Value}%"; heightValue.TextAlign = ContentAlignment.MiddleRight;
        heightRow.Controls.Add(voxelHeight, 0, 0); heightRow.Controls.Add(heightValue, 1, 0); vf.Controls.Add(heightRow); root.Controls.Add(voxelCard, 0, 1);

        var displayCard = MakeCard("DISPLAY & SOUND"); var df = (FlowLayoutPanel)displayCard.Controls[0];
        df.Controls.Add(MakeFieldLabel("Display mode")); fullscreen.DropDownStyle = ComboBoxStyle.DropDownList;
        fullscreen.Items.AddRange(["Windowed", "Borderless fullscreen", "Exclusive fullscreen"]); StyleCombo(fullscreen); df.Controls.Add(fullscreen);
        df.Controls.Add(MakeFieldLabel("Window scale")); windowScale.Minimum = 1; windowScale.Maximum = 8; windowScale.Width = 340; windowScale.BackColor = Night; windowScale.ForeColor = Color.White;
        df.Controls.Add(windowScale); df.Controls.Add(filtering); df.Controls.Add(audio); root.Controls.Add(displayCard, 1, 1); root.SetColumnSpan(displayCard, 2);
        var back = MakeActionButton("BACK", Color.FromArgb(30, 45, 62), Color.White); var save = MakeActionButton("SAVE SETTINGS", Gold, Night);
        back.Click += (_, _) => ShowHome(); save.Click += (_, _) => { SaveSettings(); ShowHome(); };
        root.Controls.Add(back, 1, 2); root.Controls.Add(save, 2, 2);
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
        voxel.Checked = ReadBool(ini.Get("Graphics", "VoxelMode", "true")); flatHud.Checked = !ReadBool(ini.Get("Graphics", "VoxelizeHud", "false"));
        filtering.Checked = ReadBool(ini.Get("Graphics", "LinearFiltering", "false")); audio.Checked = ReadBool(ini.Get("Sound", "EnableAudio", "true"));
        var size = int.TryParse(ini.Get("Graphics", "VoxelSize", "4"), out var parsedSize) ? parsedSize : 4;
        voxelSize.SelectedIndex = size switch { 2 => 0, 3 => 1, 6 => 3, 8 => 4, _ => 2 };
        voxelHeight.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "VoxelHeight", "55"), out var h) ? h : 55, 5, 100);
        fullscreen.SelectedIndex = Math.Clamp(int.TryParse(ini.Get("Graphics", "Fullscreen", "0"), out var f) ? f : 0, 0, 2);
        windowScale.Value = Math.Clamp(int.TryParse(ini.Get("Graphics", "WindowScale", "2"), out var s) ? s : 2, 1, 8);
    }

    private void SaveSettings()
    {
        var ini = new IniFile(Path.Combine(gameDirectory, "zelda3.ini")); var sizes = new[] { 2, 3, 4, 6, 8 };
        ini.Set("Graphics", "OutputMethod", "OpenGL"); ini.Set("Graphics", "NewRenderer", "true"); ini.Set("Graphics", "VoxelMode", Bool(voxel.Checked));
        ini.Set("Graphics", "VoxelizeHud", Bool(!flatHud.Checked)); ini.Set("Graphics", "VoxelSize", sizes[Math.Max(0, voxelSize.SelectedIndex)].ToString());
        ini.Set("Graphics", "VoxelHeight", voxelHeight.Value.ToString()); ini.Set("Graphics", "VoxelHudHeight", "48"); ini.Set("Graphics", "Fullscreen", fullscreen.SelectedIndex.ToString());
        ini.Set("Graphics", "WindowScale", windowScale.Value.ToString()); ini.Set("Graphics", "LinearFiltering", Bool(filtering.Checked)); ini.Set("Sound", "EnableAudio", Bool(audio.Checked)); ini.Save();
    }

    private void PlayGame()
    {
        var game = Path.Combine(gameDirectory, "zelda3.exe"); var assets = Path.Combine(gameDirectory, "zelda3_assets.dat");
        if (!File.Exists(game) || !File.Exists(assets)) { MessageBox.Show("The launcher must sit beside zelda3.exe and zelda3_assets.dat.", "Game files missing", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
        SaveSettings(); Process.Start(new ProcessStartInfo(game) { WorkingDirectory = gameDirectory, UseShellExecute = true }); status.Text = "GAME RUNNING"; status.ForeColor = Mint;
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
