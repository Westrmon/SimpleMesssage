using PluginSharp.Export;
using System;

namespace PluginSharp.Base;

/// <summary>所有插件的公共基类。</summary>
public abstract class PluginBase
{
    public abstract PluginMetaInfo Meta { get; }

    public virtual bool Initialize (PluginHost host) => true;

    public abstract StartResult Start (PluginHost host, StartOutput output);

    public virtual void Stop ()
    {
    }

    public virtual void Exit ()
    {
    }

    public virtual void OnNotify (Notification notification)
    {
    }

    public virtual void OnAction (Notification notification, string actionId)
    {
    }
}

public static class PluginEntry
{
    private static Func<PluginBase>? _factory;

    public static void Register<T> () where T : PluginBase, new()
        => _factory = CreateInstance<T>;

    public static PluginBase Create ()
        => _factory?.Invoke() ?? throw new InvalidOperationException("没有注册插件。");

    private static PluginBase CreateInstance<T> () where T : PluginBase, new()
        => new T();
}
