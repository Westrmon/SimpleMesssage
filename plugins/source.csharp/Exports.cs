// SimpleMessage — C# / Native AOT 插件：与 C++ 宿主的边界层

using PluginSharp.Base;
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace PluginSharp.Export;

public enum PluginKind : int
{
    Source = 0,
    Notifier = 1,
}

public enum StartResult : int
{
    Ok = 0,
    Empty = 1,
    Error = 2,
    Abort = 3,
    Cancelled = 4,
}

public enum SettingType : int
{
    Bool = 0,
    Int = 1,
    Double = 2,
    String = 3,
    Enum = 4,
    Sound = 5,
}

public enum Level : int
{
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4,
}

public enum LogLevel : int
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
}

public sealed class PluginSettingOption
{
    public string Value { get; set; } = "";
    public string Label { get; set; } = "";
}

public sealed class PluginSetting
{
    public string Name { get; set; } = "";
    public string Label { get; set; } = "";
    public string Description { get; set; } = "";
    public SettingType Type { get; set; } = SettingType.String;

    public bool RequiresRestart { get; set; }

    public bool DefaultBool { get; set; }
    public long DefaultInt { get; set; }
    public double DefaultDouble { get; set; }
    public string? DefaultString { get; set; }

    public long MinInt { get; set; }
    public long MaxInt { get; set; }
    public double MinDouble { get; set; }
    public double MaxDouble { get; set; }
    public nuint MaxStringBytes { get; set; }

    public List<PluginSettingOption> Options { get; } = new();
}

public sealed class PluginMetaInfo
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Author { get; set; } = "";
    public string Version { get; set; } = "1.0.0";
    public string Description { get; set; } = "";
    public PluginKind Kind { get; set; } = PluginKind.Source;
    public string HostRange { get; set; } = ">=0.1.0,<1.0.0";

    public List<string> Subscriptions { get; } = new();
    public List<PluginSetting> Settings { get; } = new();
}

public sealed class NotificationAction
{
    public string Id { get; set; } = "";
    public string Label { get; set; } = "";
}

public sealed class Notification
{
    public string? Title { get; set; }
    public string? Content { get; set; }

    // 留空时由宿主根据正文生成摘要。
    public string? Summary { get; set; }

    public string? Topic { get; set; }
    public Level Level { get; set; } = Level.Info;

    // 为 null 时由边界层填入 host.NowUnixMs。
    public long? TimeUnixMs { get; set; }

    // 留空表示每次都是新通知；填固定 id 时宿主会原地更新同一条。
    public string? Id { get; set; }
    public string? Url { get; set; }
    public string? Icon { get; set; }
    public List<NotificationAction> Actions { get; } = new();

    // 以下两项由宿主填写，插件构造通知时应留空。
    public string? SourcePlugin { get; set; }
    public long Seq { get; set; }
}

public sealed class StartOutput
{
    public List<Notification> Items { get; } = new();

    /// <summary>非空时宿主会记录一条错误；通常同时返回 StartResult.Error。</summary>
    public string? Error { get; set; }

    public void Add (Notification notification) => Items.Add(notification);
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmSpan<T> where T : unmanaged
{
    public T* Data;
    public nuint Length;

    public SmSpan (T* data, nuint length)
    {
        Data = data;
        Length = length;
    }
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmStringSpan
{
    public byte** Data;
    public nuint Length;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmSettingOption
{
    public byte* Value;
    public byte* Label;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmSettingDef
{
    public byte* Name;
    public byte* Label;
    public byte* Description;
    public SettingType Type;

    public byte RequiresRestart;
    public byte DefaultBool;

    public long DefaultInt;
    public double DefaultDouble;
    public byte* DefaultString;

    public long MinInt;
    public long MaxInt;
    public double MinDouble;
    public double MaxDouble;
    public nuint MaxStringBytes;

    public SmSpan<SmSettingOption> Options;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmPluginMeta
{
    public byte* Id;
    public byte* Name;
    public byte* Author;
    public byte* Version;
    public byte* Description;
    public PluginKind Kind;
    public byte* HostRange;
    public SmStringSpan Subscriptions;
    public SmSpan<SmSettingDef> Settings;
    public int ApiVersion;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmAction
{
    public byte* Id;
    public byte* Label;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmNotification
{
    public byte* Title;
    public byte* Content;
    public byte* Summary;
    public byte* Topic;
    public Level Level;
    public long TimeUnixMs;
    public byte* Id;
    public byte* Url;
    public byte* Icon;
    public SmSpan<SmAction> Actions;
    public byte* SourcePlugin;
    public long Seq;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct SmStartOutput
{
    public SmSpan<SmNotification> Items;
    public byte* Error;
}


[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PluginVTable
{
    public delegate* unmanaged<nint, uint, nint> Destroy;
    public delegate* unmanaged<nint, SmPluginMeta*> Meta;
    public delegate* unmanaged<nint, byte> Initialize;
    public delegate* unmanaged<nint, SmStartOutput*, int> Start;
    public delegate* unmanaged<nint, void> Stop;
    public delegate* unmanaged<nint, void> Exit;
    public delegate* unmanaged<nint, SmNotification*, void> OnNotify;
    public delegate* unmanaged<nint, SmNotification*, byte*, void> OnAction;
    public delegate* unmanaged<nint, SmNotification*, nuint, void> DrawUi;
    public delegate* unmanaged<nint, void> DrawView;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct PluginInstance
{
    public PluginVTable* VTable;
    public nint Host;
    public nint PluginHandle;
}

public readonly unsafe struct PluginHost
{
    private const int VtNowUnixMs = 7;
    private const int VtAllocate = 9;
    private const int VtScheduleNext = 10;
    private const int VtSettingBool = 12;
    private const int VtSettingInt = 13;
    private const int VtSettingDouble = 14;
    private const int VtSettingString = 15;
    private const int VtSettingEnum = 16;
    private const int VtLog = 17;
    private const int VtIsCancelled = 20;

    private readonly nint _host;

    internal PluginHost (nint host)
    {
        _host = host;
    }

    private nint Slot (int index) => ((nint*)(*(nint*)_host))[index];

    public long NowUnixMs =>
        ((delegate* unmanaged<nint, long>)Slot(VtNowUnixMs))(_host);

    public bool IsCancelled =>
        ((delegate* unmanaged<nint, byte>)Slot(VtIsCancelled))(_host) != 0;

    public void ScheduleNext (long delayMs) =>
        ((delegate* unmanaged<nint, long, void>)Slot(VtScheduleNext))(_host, delayMs);

    public bool GetSettingBool (string name, bool fallback)
    {
        byte[] nameBytes = ToUtf8Z(name);
        fixed (byte* namePointer = nameBytes)
        {
            return ((delegate* unmanaged<nint, byte*, byte, byte>)Slot(VtSettingBool))(
                _host, namePointer, fallback ? (byte)1 : (byte)0) != 0;
        }
    }

    public long GetSettingInt (string name, long fallback)
    {
        byte[] nameBytes = ToUtf8Z(name);
        fixed (byte* namePointer = nameBytes)
        {
            return ((delegate* unmanaged<nint, byte*, long, long>)Slot(VtSettingInt))(
                _host, namePointer, fallback);
        }
    }

    public double GetSettingDouble (string name, double fallback)
    {
        byte[] nameBytes = ToUtf8Z(name);
        fixed (byte* namePointer = nameBytes)
        {
            return ((delegate* unmanaged<nint, byte*, double, double>)Slot(VtSettingDouble))(
                _host, namePointer, fallback);
        }
    }

    public string GetSettingString (string name, string fallback)
    {
        byte[] nameBytes = ToUtf8Z(name);
        byte[] fallbackBytes = ToUtf8Z(fallback);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* fallbackPointer = fallbackBytes)
        {
            byte* result = ((delegate* unmanaged<nint, byte*, byte*, byte*>)Slot(
                VtSettingString))(_host, namePointer, fallbackPointer);
            return Utf8ToString(result) ?? fallback;
        }
    }

    public string GetSettingEnum (string name, string fallback)
    {
        byte[] nameBytes = ToUtf8Z(name);
        byte[] fallbackBytes = ToUtf8Z(fallback);
        fixed (byte* namePointer = nameBytes)
        fixed (byte* fallbackPointer = fallbackBytes)
        {
            byte* result = ((delegate* unmanaged<nint, byte*, byte*, byte*>)Slot(
                VtSettingEnum))(_host, namePointer, fallbackPointer);
            return Utf8ToString(result) ?? fallback;
        }
    }

    public void Log (LogLevel level, string message)
    {
        byte[] messageBytes = ToUtf8Z(message);
        fixed (byte* messagePointer = messageBytes)
        {
            ((delegate* unmanaged<nint, int, byte*, void>)Slot(VtLog))(
                _host, (int)level, messagePointer);
        }
    }

    internal byte* Allocate (nuint bytes) =>
        ((delegate* unmanaged<nint, nuint, byte*>)Slot(VtAllocate))(_host, bytes);

    private static byte[] ToUtf8Z (string value)
    {
        int count = Encoding.UTF8.GetByteCount(value);
        var bytes = new byte[count + 1];
        Encoding.UTF8.GetBytes(value, 0, value.Length, bytes, 0);
        bytes[count] = 0;
        return bytes;
    }

    private static string? Utf8ToString (byte* value) =>
        value == null ? null : Marshal.PtrToStringUTF8((nint)value);
}

internal static unsafe class NativeExports
{
    private const int ApiVersion = 2;

    private static int _initialized;
    private static readonly object InitLock = new();

    private static PluginVTable* _vtable;
    private static SmPluginMeta* _meta;
    private static byte* _errorOutOfMemory;

    [UnmanagedCallersOnly(EntryPoint = "sm_abi_version")]
    public static int AbiVersion () => ApiVersion;

    [UnmanagedCallersOnly(EntryPoint = "sm_plugin_meta")]
    public static nint PluginMetaExport ()
    {
        try
        {
            EnsureInit();
            return (nint)_meta;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "sm_create_plugin")]
    public static nint CreatePlugin (nint host)
    {
        try
        {
            EnsureInit();
            PluginBase plugin = PluginEntry.Create();
            var instance = (PluginInstance*)NativeMemory.Alloc(
                (nuint)sizeof(PluginInstance));
            if (instance == null) return 0;

            GCHandle handle = GCHandle.Alloc(plugin, GCHandleType.Normal);

            instance->VTable = _vtable;
            instance->Host = host;
            instance->PluginHandle = GCHandle.ToIntPtr(handle);
            return (nint)instance;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "sm_destroy_plugin")]
    public static void DestroyPlugin (nint plugin)
    {
        if (plugin == 0) return;

        try
        {
            var instance = (PluginInstance*)plugin;
            if (instance->PluginHandle != 0)
            {
                GCHandle.FromIntPtr(instance->PluginHandle).Free();
                instance->PluginHandle = 0;
            }
        }
        catch
        {
        }

        NativeMemory.Free((void*)plugin);
    }

    private static void EnsureInit ()
    {
        if (Volatile.Read(ref _initialized) != 0) return;
        lock (InitLock)
        {
            if (Volatile.Read(ref _initialized) != 0) return;

            _errorOutOfMemory = AllocUtf8("宿主 arena 分配失败");
            _meta = BuildMeta(PluginEntry.Create().Meta);
            _vtable = (PluginVTable*)NativeMemory.Alloc((nuint)sizeof(PluginVTable));
            if (_vtable == null) throw new OutOfMemoryException();
            _vtable->Destroy = &DestroyThunk;
            _vtable->Meta = &MetaThunk;
            _vtable->Initialize = &InitializeThunk;
            _vtable->Start = &StartThunk;
            _vtable->Stop = &StopThunk;
            _vtable->Exit = &ExitThunk;
            _vtable->OnNotify = &OnNotifyThunk;
            _vtable->OnAction = &OnActionThunk;
            _vtable->DrawUi = &DrawUiThunk;
            _vtable->DrawView = &DrawViewThunk;

            Volatile.Write(ref _initialized, 1);
        }
    }

    [UnmanagedCallersOnly]
    private static nint DestroyThunk (nint self, uint flags) => self;

    [UnmanagedCallersOnly]
    private static SmPluginMeta* MetaThunk (nint self)
    {
        try
        {
            EnsureInit();
            return _meta;
        }
        catch
        {
            return null;
        }
    }

    [UnmanagedCallersOnly]
    private static byte InitializeThunk (nint self)
    {
        try
        {
            var instance = (PluginInstance*)self;
            bool ok = GetPlugin(self).Initialize(new PluginHost(instance->Host));
            return ok ? (byte)1 : (byte)0;
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    private static int StartThunk (nint self, SmStartOutput* nativeOutput)
    {
        try
        {
            var instance = (PluginInstance*)self;
            var host = new PluginHost(instance->Host);
            var output = new StartOutput();

            StartResult result = GetPlugin(self).Start(host, output);

            if (!TryMarshalStartOutput(host, output, nativeOutput))
            {
                if (nativeOutput != null)
                {
                    nativeOutput->Items = default;
                    TryCopyString(host, "宿主 arena 分配失败", 128,
                                  out byte* error);
                    nativeOutput->Error = error;
                }
                return (int)StartResult.Error;
            }

            return (int)result;
        }
        catch
        {
            if (nativeOutput != null)
            {
                nativeOutput->Items = default;
                nativeOutput->Error = null;
            }
            return (int)StartResult.Error;
        }
    }

    [UnmanagedCallersOnly]
    private static void StopThunk (nint self)
    {
        try
        {
            GetPlugin(self).Stop();
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    private static void ExitThunk (nint self)
    {
        try
        {
            GetPlugin(self).Exit();
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    private static void OnNotifyThunk (nint self, SmNotification* notification)
    {
        try
        {
            if (notification == null) return;
            GetPlugin(self).OnNotify(ToManagedNotification(notification));
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    private static void OnActionThunk (nint self, SmNotification* notification,
                                      byte* actionId)
    {
        try
        {
            if (notification == null) return;
            string id = Utf8ToString(actionId) ?? "";
            GetPlugin(self).OnAction(ToManagedNotification(notification), id);
        }
        catch
        {
        }
    }

    [UnmanagedCallersOnly]
    private static void DrawUiThunk (nint self, SmNotification* history, nuint count)
    {
    }

    [UnmanagedCallersOnly]
    private static void DrawViewThunk (nint self)
    {
    }

    private static PluginBase GetPlugin (nint self)
    {
        var instance = (PluginInstance*)self;
        return (PluginBase)GCHandle.FromIntPtr(instance->PluginHandle).Target!;
    }

    private static SmPluginMeta* BuildMeta (PluginMetaInfo info)
    {
        int settingCount = info.Settings.Count;
        SmSettingDef* settings = null;
        if (settingCount > 0)
        {
            settings = (SmSettingDef*)NativeMemory.Alloc(
                (nuint)(sizeof(SmSettingDef) * settingCount));
            if (settings == null) throw new OutOfMemoryException();

            for (int i = 0; i < settingCount; i++)
            {
                settings[i] = BuildSetting(info.Settings[i]);
            }
        }

        int subscriptionCount = info.Subscriptions.Count;
        byte** subscriptions = null;
        if (subscriptionCount > 0)
        {
            subscriptions = (byte**)NativeMemory.Alloc(
                (nuint)(sizeof(byte*) * subscriptionCount));
            if (subscriptions == null) throw new OutOfMemoryException();

            for (int i = 0; i < subscriptionCount; i++)
            {
                subscriptions[i] = AllocUtf8(info.Subscriptions[i]);
            }
        }

        var meta = (SmPluginMeta*)NativeMemory.Alloc((nuint)sizeof(SmPluginMeta));
        if (meta == null) throw new OutOfMemoryException();

        *meta = new SmPluginMeta
        {
            Id = AllocUtf8(info.Id),
            Name = AllocUtf8(info.Name),
            Author = AllocUtf8(info.Author),
            Version = AllocUtf8(info.Version),
            Description = AllocUtf8(info.Description),
            Kind = info.Kind,
            HostRange = AllocUtf8(info.HostRange),
            Subscriptions = subscriptionCount > 0
                ? new SmStringSpan
                {
                    Data = subscriptions,
                    Length = (nuint)subscriptionCount,
                }
                : default,
            Settings = settingCount > 0
                ? new SmSpan<SmSettingDef>(settings, (nuint)settingCount)
                : default,
            ApiVersion = ApiVersion,
        };
        return meta;
    }

    private static SmSettingDef BuildSetting (PluginSetting setting)
    {
        var options = default(SmSpan<SmSettingOption>);
        int optionCount = setting.Options.Count;
        if (optionCount > 0)
        {
            var array = (SmSettingOption*)NativeMemory.Alloc(
                (nuint)(sizeof(SmSettingOption) * optionCount));
            if (array == null) throw new OutOfMemoryException();

            for (int i = 0; i < optionCount; i++)
            {
                array[i] = new SmSettingOption
                {
                    Value = AllocUtf8(setting.Options[i].Value),
                    Label = AllocUtf8(setting.Options[i].Label),
                };
            }
            options = new SmSpan<SmSettingOption>(array, (nuint)optionCount);
        }

        return new SmSettingDef
        {
            Name = AllocUtf8(setting.Name),
            Label = AllocUtf8(setting.Label),
            Description = AllocUtf8(setting.Description),
            Type = setting.Type,
            RequiresRestart = setting.RequiresRestart ? (byte)1 : (byte)0,
            DefaultBool = setting.DefaultBool ? (byte)1 : (byte)0,
            DefaultInt = setting.DefaultInt,
            DefaultDouble = setting.DefaultDouble,
            DefaultString = AllocUtf8(setting.DefaultString),
            MinInt = setting.MinInt,
            MaxInt = setting.MaxInt,
            MinDouble = setting.MinDouble,
            MaxDouble = setting.MaxDouble,
            MaxStringBytes = setting.MaxStringBytes,
            Options = options,
        };
    }

    /// <summary>
    /// 把 StartOutput 写进宿主的 SmStartOutput。
    /// 返回 false 表示 arena 分配失败（调用方负责写 Error）。
    /// </summary>
    private static bool TryMarshalStartOutput (PluginHost host, StartOutput output,
                                              SmStartOutput* nativeOutput)
    {
        if (nativeOutput == null) return true;

        nativeOutput->Items = default;
        nativeOutput->Error = null;

        if (!string.IsNullOrEmpty(output.Error))
        {
            TryCopyString(host, output.Error, 512, out byte* error);
            nativeOutput->Error = error;
            return true;
        }

        int count = output.Items.Count;
        if (count == 0) return true;

        var items = (SmNotification*)host.Allocate(
            (nuint)(sizeof(SmNotification) * count));
        if (items == null) return false;

        for (int i = 0; i < count; i++)
        {
            if (!TryMarshalNotification(host, output.Items[i], &items[i]))
            {
                return false;
            }
        }

        nativeOutput->Items = new SmSpan<SmNotification>(items, (nuint)count);
        return true;
    }

    private static bool TryMarshalNotification (PluginHost host, Notification source,
                                               SmNotification* destination)
    {
        *destination = default;
        destination->Level = source.Level;
        destination->TimeUnixMs = source.TimeUnixMs ?? host.NowUnixMs;

        if (!TryCopyString(host, source.Title, 256, out byte* title)) return false;
        if (!TryCopyString(host, source.Content, 64 * 1024, out byte* content))
            return false;
        if (!TryCopyString(host, source.Summary, 512, out byte* summary))
            return false;
        if (!TryCopyString(host, source.Topic, 128, out byte* topic)) return false;
        if (!TryCopyString(host, source.Id, 256, out byte* id)) return false;
        if (!TryCopyString(host, source.Url, 2048, out byte* url)) return false;
        if (!TryCopyString(host, source.Icon, 1024, out byte* icon)) return false;

        destination->Title = title;
        destination->Content = content;
        destination->Summary = summary;
        destination->Topic = topic;
        destination->Id = id;
        destination->Url = url;
        destination->Icon = icon;

        int actionCount = source.Actions.Count;
        if (actionCount > 0)
        {
            var actions = (SmAction*)host.Allocate(
                (nuint)(sizeof(SmAction) * actionCount));
            if (actions == null) return false;

            for (int i = 0; i < actionCount; i++)
            {
                if (!TryCopyString(host, source.Actions[i].Id, 128, out byte* actionId))
                    return false;
                if (!TryCopyString(host, source.Actions[i].Label, 256,
                                   out byte* label))
                    return false;

                actions[i] = new SmAction { Id = actionId, Label = label };
            }

            destination->Actions = new SmSpan<SmAction>(actions, (nuint)actionCount);
        }

        return true;
    }

    /// <summary>
    /// 把托管字符串复制进本次回调的 arena。截断按 UTF-8 码点边界做，
    /// 不会把一个中文字符切成半个。
    /// </summary>
    private static bool TryCopyString (PluginHost host, string? value,
                                      nuint maxBytes, out byte* result)
    {
        result = null;
        if (value == null) return true;

        byte[] bytes = Encoding.UTF8.GetBytes(value);
        nuint length = 0;
        while (length < (nuint)bytes.Length)
        {
            int sequenceLength = Utf8SequenceLength(bytes[(int)length]);
            if (length + (nuint)sequenceLength > maxBytes) break;
            length += (nuint)sequenceLength;
        }

        byte* destination = host.Allocate(length + 1);
        if (destination == null) return false;

        if (length > 0)
        {
            fixed (byte* sourceBytes = bytes)
            {
                Buffer.MemoryCopy(sourceBytes, destination, (long)(length + 1),
                                  (long)length);
            }
        }
        destination[length] = 0;

        result = destination;
        return true;
    }

    private static int Utf8SequenceLength (byte lead)
    {
        if (lead < 0x80) return 1;
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        if ((lead & 0xF8) == 0xF0) return 4;
        return 1;
    }

    private static Notification ToManagedNotification (SmNotification* source)
    {
        var notification = new Notification
        {
            Title = Utf8ToString(source->Title),
            Content = Utf8ToString(source->Content),
            Summary = Utf8ToString(source->Summary),
            Topic = Utf8ToString(source->Topic),
            Level = source->Level,
            TimeUnixMs = source->TimeUnixMs,
            Id = Utf8ToString(source->Id),
            Url = Utf8ToString(source->Url),
            Icon = Utf8ToString(source->Icon),
            SourcePlugin = Utf8ToString(source->SourcePlugin),
            Seq = source->Seq,
        };

        int actionCount = (int)source->Actions.Length;
        for (int i = 0; i < actionCount; i++)
        {
            SmAction action = source->Actions.Data[i];
            notification.Actions.Add(new NotificationAction
            {
                Id = Utf8ToString(action.Id) ?? "",
                Label = Utf8ToString(action.Label) ?? "",
            });
        }

        return notification;
    }

    private static string? Utf8ToString (byte* value) =>
        value == null ? null : Marshal.PtrToStringUTF8((nint)value);

    /// <summary>把一个 C# 字符串复制成模块级非托管 UTF-8 字符串。</summary>
    private static byte* AllocUtf8 (string? value)
    {
        if (value == null) return null;

        byte[] bytes = Encoding.UTF8.GetBytes(value);
        var pointer = (byte*)NativeMemory.Alloc((nuint)bytes.Length + 1);
        if (pointer == null) throw new OutOfMemoryException();

        if (bytes.Length > 0)
        {
            Marshal.Copy(bytes, 0, (IntPtr)pointer, bytes.Length);
        }
        pointer[bytes.Length] = 0;
        return pointer;
    }
}