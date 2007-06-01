/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Vamp

    An API for audio analysis and feature extraction plugins.

    Centre for Digital Music, Queen Mary, University of London.
    Copyright 2006 Chris Cannam.
  
    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
    ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the names of the Centre for
    Digital Music; Queen Mary, University of London; and Chris Cannam
    shall not be used in advertising or otherwise to promote the sale,
    use or other dealings in this Software without prior written
    authorization.
*/

#ifndef _VAMP_PLUGIN_LOADER_H_
#define _VAMP_PLUGIN_LOADER_H_

#include <vector>
#include <string>
#include <map>

#include "PluginWrapper.h"

namespace Vamp {

class Plugin;

namespace HostExt {

/**
 * Vamp::HostExt::PluginLoader is a convenience class for discovering
 * and loading Vamp plugins using the typical plugin-path, library
 * naming, and categorisation conventions described in the Vamp SDK
 * documentation.  This class is intended to greatly simplify the task
 * of becoming a Vamp plugin host for any C++ application.
 * 
 * Hosts are not required by the Vamp specification to use the same
 * plugin search path and naming conventions as implemented by this
 * class, and are certainly not required to use this actual class.
 * But it's recommended, for sound practical reasons.
 */

class PluginLoader
{
public:
    /**
     * PluginLoader is a singleton class.  This function returns a
     * pointer to the single instance of it.  Use this to obtain your
     * loader object.
     */
    static PluginLoader *getInstance();

    /**
     * PluginKey is a string type that is used to identify a plugin
     * uniquely within the scope of "the current system".  It consists
     * of the base name of the plugin library, a colon separator, and
     * the identifier string for the plugin.  It is only meaningful in
     * the context of a given plugin path (the one returned by
     * PluginHostAdapter::getPluginPath()).
     *
     * Use composePluginKey to construct a plugin key from a known
     * plugin library name and identifier.
     */
    typedef std::string PluginKey;

    /**
     * PluginKeyList is a sequence of plugin keys, such as returned by
     * a plugin lookup function.
     */
    typedef std::vector<PluginKey> PluginKeyList;

    /**
     * PluginCategoryHierarchy is a sequence of general->specific
     * category names, as may be associated with a single plugin.
     * This sequence describes the location of a plugin within a
     * category forest, containing the human-readable names of the
     * plugin's category tree root, followed by each of the nodes down
     * to the leaf containing the plugin.
     */
    typedef std::vector<std::string> PluginCategoryHierarchy;

    /**
     * Search for all available Vamp plugins, and return a list of
     * them in the order in which they were found.
     */
    PluginKeyList listPlugins();

    /**
     * AdapterFlags contains a set of values that may be OR'd together
     * to indicate in which circumstances PluginLoader should use a
     * plugin adapter to make a plugin easier to use for a host that
     * does not want to cater for complex features.
     *
     * The available flags are:
     * 
     * ADAPT_INPUT_DOMAIN - If the plugin expects frequency domain
     * input, wrap it in a PluginInputDomainAdapter that automatically
     * converts the plugin to one that expects time-domain input.
     * This enables a host to accommodate time- and frequency-domain
     * plugins without needing to do any conversion itself.
     *
     * ADAPT_CHANNEL_COUNT - Wrap the plugin in a PluginChannelAdapter
     * to handle any mismatch between the number of channels of audio
     * the plugin can handle and the number available in the host.
     * This enables a host to use plugins that may require the input
     * to be mixed down to mono, etc., without having to worry about
     * doing that itself.
     * 
     * See PluginInputDomainAdapter and PluginChannelAdapter for more
     * details of the classes that the loader may use if these flags
     * are set.
     */
    enum AdapterFlags {
        ADAPT_INPUT_DOMAIN  = 0x01,
        ADAPT_CHANNEL_COUNT = 0x02,
        ADAPT_ALL           = 0xff
    };

    /**
     * Load a Vamp plugin, given its identifying key.  If the plugin
     * could not be loaded, returns 0.
     *
     * adapterFlags is a bitwise OR of the values in the AdapterFlags
     * enum, indicating under which circumstances an adapter should be
     * used to wrap the original plugin.  See AdapterFlags for more
     * details.  If adapterFlags is 0, no optional adapters will be
     * used.
     *
     * The returned plugin should be deleted (using the standard C++
     * delete) after use.
     */
    Plugin *loadPlugin(PluginKey key,
                       float inputSampleRate,
                       int adapterFlags = 0);

    /**
     * Given a Vamp plugin library name and plugin identifier, return
     * the corresponding plugin key in a form suitable for passing in to
     * loadPlugin.
     */
    PluginKey composePluginKey(std::string libraryName,
                               std::string identifier);

    /**
     * Return the category hierarchy for a Vamp plugin, given its
     * identifying key.  See PluginCategoryHierarchy documentation for
     * more details.
     *
     * If the plugin has no category information, return an empty
     * hierarchy.
     */
    PluginCategoryHierarchy getPluginCategory(PluginKey plugin);

    /**
     * Return the file path of the dynamic library from which the
     * given plugin will be loaded (if available).
     */
    std::string getLibraryPathForPlugin(PluginKey plugin);

protected:
    PluginLoader();
    virtual ~PluginLoader();

    class PluginDeletionNotifyAdapter : public PluginWrapper {
    public:
        PluginDeletionNotifyAdapter(Plugin *plugin, PluginLoader *loader);
        virtual ~PluginDeletionNotifyAdapter();
    protected:
        PluginLoader *m_loader;
    };

    virtual void pluginDeleted(PluginDeletionNotifyAdapter *adapter);

    std::map<PluginKey, std::string> m_pluginLibraryNameMap;
    void generateLibraryMap();

    std::map<PluginKey, PluginCategoryHierarchy> m_taxonomy;
    void generateTaxonomy();

    std::map<Plugin *, void *> m_pluginLibraryHandleMap;

    void *loadLibrary(std::string path);
    void unloadLibrary(void *handle);
    void *lookupInLibrary(void *handle, const char *symbol);

    std::string splicePath(std::string a, std::string b);
    std::vector<std::string> listFiles(std::string dir, std::string ext);

    static PluginLoader *m_instance;
};

}

}

#endif

