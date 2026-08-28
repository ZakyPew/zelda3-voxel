namespace Zelda3VoxelLauncher;

internal sealed class IniFile
{
    private readonly string path;
    private readonly List<string> lines;

    public IniFile(string path)
    {
        this.path = path;
        lines = File.Exists(path) ? File.ReadAllLines(path).ToList() : new List<string>();
    }

    public string Get(string section, string key, string fallback)
    {
        var current = string.Empty;
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.StartsWith('[') && line.EndsWith(']'))
                current = line[1..^1];
            else if (current.Equals(section, StringComparison.OrdinalIgnoreCase))
            {
                var split = line.IndexOf('=');
                if (split > 0 && line[..split].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                    return line[(split + 1)..].Trim();
            }
        }
        return fallback;
    }

    public void Set(string section, string key, string value)
    {
        var header = $"[{section}]";
        var sectionStart = lines.FindIndex(x => x.Trim().Equals(header, StringComparison.OrdinalIgnoreCase));
        if (sectionStart < 0)
        {
            if (lines.Count > 0 && lines[^1].Length != 0) lines.Add(string.Empty);
            lines.Add(header);
            lines.Add($"{key}={value}");
            return;
        }

        var sectionEnd = lines.FindIndex(sectionStart + 1, x => x.TrimStart().StartsWith('['));
        if (sectionEnd < 0) sectionEnd = lines.Count;
        for (var i = sectionStart + 1; i < sectionEnd; i++)
        {
            var split = lines[i].IndexOf('=');
            if (split > 0 && lines[i][..split].Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
            {
                lines[i] = $"{key}={value}";
                return;
            }
        }
        lines.Insert(sectionEnd, $"{key}={value}");
    }

    public void Save() => File.WriteAllLines(path, lines);
}
