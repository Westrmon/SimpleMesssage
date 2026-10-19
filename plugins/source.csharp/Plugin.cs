using PluginSharp.Base;
using PluginSharp.Export;
using PluginSharp.Source;
using System;
using System.Runtime.CompilerServices;

namespace Source;

/// <summary>C# 示例数据源：按固定间隔产生一条通知。</summary>
public sealed class SourceCsharpPlugin : SourcePlugin
{
    private const string DefaultTitleText = "C# 插件提醒";
    private const string DefaultMessageText = "这是来自 C# AOT 插件的定时通知。";
    private const long DefaultIntervalSeconds = 60;
    private const long MinIntervalSeconds = 5;
    private const long MaxIntervalSeconds = 86400;

    public override PluginMetaInfo Meta { get; } = new()
    {
        Id = "source.csharp",
        Name = "C# 定时源",
        Author = "SimpleMessage 示例",
        Version = "1.0.0",
        Description = "用 C# / Native AOT 编写的示例 Source：按固定间隔产生通知。",
        Kind = PluginKind.Source,
        HostRange = ">=0.1.0,<1.0.0",
        Settings =
        {
            new PluginSetting
            {
                Name = "interval_sec",
                Label = "间隔（秒）",
                Description = "每隔多少秒产生一条通知。",
                Type = SettingType.Int,
                DefaultInt = DefaultIntervalSeconds,
                MinInt = MinIntervalSeconds,
                MaxInt = MaxIntervalSeconds,
            },
            new PluginSetting
            {
                Name = "title",
                Label = "标题",
                Description = "通知标题。为空时使用默认值。",
                Type = SettingType.String,
                DefaultString = DefaultTitleText,
                MaxStringBytes = 200,
            },
            new PluginSetting
            {
                Name = "message",
                Label = "通知内容",
                Description = "每次产生通知时使用的正文。为空时使用默认值。",
                Type = SettingType.String,
                DefaultString = DefaultMessageText,
                MaxStringBytes = 800,
            },
        },
    };

    public override bool Initialize (PluginHost host)
    {
        host.Log(LogLevel.Info, "C# 插件已就绪");
        return true;
    }

    public override StartResult Start (PluginHost host, StartOutput output)
    {
        if (host.IsCancelled) return StartResult.Cancelled;

        long interval = host.GetSettingInt("interval_sec", DefaultIntervalSeconds);
        interval = Math.Clamp(interval, MinIntervalSeconds, MaxIntervalSeconds);

        string title = host.GetSettingString("title", DefaultTitleText);
        if (string.IsNullOrEmpty(title)) title = DefaultTitleText;

        string message = host.GetSettingString("message", DefaultMessageText);
        if (string.IsNullOrEmpty(message)) message = DefaultMessageText;

        output.Add(new Notification
        {
            Title = title,
            Content = message,
            Topic = "example.csharp.tick",
            Level = Level.Info,
            TimeUnixMs = host.NowUnixMs,
        });

        host.ScheduleNext(interval * 1000);
        return StartResult.Ok;
    }
}

internal static class PluginRegistration
{
    [ModuleInitializer]
    internal static void Register () => PluginEntry.Register<SourceCsharpPlugin>();
}
