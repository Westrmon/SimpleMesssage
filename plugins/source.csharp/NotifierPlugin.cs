using PluginSharp.Base;
using PluginSharp.Export;

namespace PluginSharp.Notifier;

/// <summary>通知器插件基类：接收并处理宿主产生的通知。</summary>
public abstract class NotifierPlugin : PluginBase
{
    public sealed override StartResult Start (PluginHost host, StartOutput output)
        => StartResult.Empty;

    public abstract override void OnNotify (Notification notification);
}
