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

#include "vamp-sdk/PluginHostAdapter.h"
#include "PluginLoader.h"
#include "PluginInputDomainAdapter.h"
#include "PluginChannelAdapter.h"

#include <fstream>

#ifdef _WIN32

#include <windows.h>
#include <tchar.h>
#define PLUGIN_SUFFIX "dll"

#else /* ! _WIN32 */

#include <dirent.h>
#include <dlfcn.h>

#ifdef __APPLE__
#define PLUGIN_SUFFIX "dylib"
#else /* ! __APPLE__ */
#define PLUGIN_SUFFIX "so"
#endif /* ! __APPLE__ */

#endif /* ! _WIN32 */

using namespace std;

namespace Vamp {
	
namespace HostExt {

PluginLoader *
PluginLoader::m_instance = 0;

PluginLoader::PluginLoader()
{
}

PluginLoader::~PluginLoader()
{
}

PluginLoader *
PluginLoader::getInstance()
{
    if (!m_instance) m_instance = new PluginLoader();
    return m_instance;
}

vector<PluginLoader::PluginKey>
PluginLoader::listPlugins() 
{
    if (m_pluginLibraryNameMap.empty()) generateLibraryMap();

    vector<PluginKey> plugins;
    for (map<PluginKey, string>::iterator mi =
             m_pluginLibraryNameMap.begin();
         mi != m_pluginLibraryNameMap.end(); ++mi) {
        plugins.push_back(mi->first);
    }

    return plugins;
}

void
PluginLoader::generateLibraryMap()
{
    vector<string> path = PluginHostAdapter::getPluginPath();

    for (size_t i = 0; i < path.size(); ++i) {
        
        vector<string> files = listFiles(path[i], PLUGIN_SUFFIX);

        for (vector<string>::iterator fi = files.begin();
             fi != files.end(); ++fi) {
            
            string fullPath = path[i];
            fullPath = splicePath(fullPath, *fi);
            void *handle = loadLibrary(fullPath);
            if (!handle) continue;
            
            VampGetPluginDescriptorFunction fn =
                (VampGetPluginDescriptorFunction)lookupInLibrary
                (handle, "vampGetPluginDescriptor");
            
            if (!fn) {
                unloadLibrary(handle);
                continue;
            }
            
            int index = 0;
            const VampPluginDescriptor *descriptor = 0;
            
            while ((descriptor = fn(VAMP_API_VERSION, index))) {
                PluginKey key = composePluginKey(*fi, descriptor->identifier);
                if (m_pluginLibraryNameMap.find(key) ==
                    m_pluginLibraryNameMap.end()) {
                    m_pluginLibraryNameMap[key] = fullPath;
                }
                ++index;
            }
            
            unloadLibrary(handle);
        }
    }
}

PluginLoader::PluginKey
PluginLoader::composePluginKey(string libraryName, string identifier)
{
    string basename = libraryName;

    string::size_type li = basename.rfind('/');
    if (li != string::npos) basename = basename.substr(li + 1);

    li = basename.find('.');
    if (li != string::npos) basename = basename.substr(0, li);

    return basename + ":" + identifier;
}

PluginLoader::PluginCategoryHierarchy
PluginLoader::getPluginCategory(PluginKey plugin)
{
    if (m_taxonomy.empty()) generateTaxonomy();
    if (m_taxonomy.find(plugin) == m_taxonomy.end()) return PluginCategoryHierarchy();
    return m_taxonomy[plugin];
}

string
PluginLoader::getLibraryPathForPlugin(PluginKey plugin)
{
    if (m_pluginLibraryNameMap.empty()) generateLibraryMap();
    if (m_pluginLibraryNameMap.find(plugin) == m_pluginLibraryNameMap.end()) return "";
    return m_pluginLibraryNameMap[plugin];
}    

Plugin *
PluginLoader::loadPlugin(PluginKey key, float inputSampleRate, int adapterFlags)
{
    string fullPath = getLibraryPathForPlugin(key);
    if (fullPath == "") return 0;
    
    string::size_type ki = key.find(':');
    if (ki == string::npos) {
        //!!! flag error
        return 0;
    }

    string identifier = key.substr(ki + 1);
    
    void *handle = loadLibrary(fullPath);
    if (!handle) return 0;
    
    VampGetPluginDescriptorFunction fn =
        (VampGetPluginDescriptorFunction)lookupInLibrary
        (handle, "vampGetPluginDescriptor");

    if (!fn) {
        unloadLibrary(handle);
        return 0;
    }

    int index = 0;
    const VampPluginDescriptor *descriptor = 0;

    while ((descriptor = fn(VAMP_API_VERSION, index))) {

        if (string(descriptor->identifier) == identifier) {

            Vamp::PluginHostAdapter *plugin =
                new Vamp::PluginHostAdapter(descriptor, inputSampleRate);

            Plugin *adapter = new PluginDeletionNotifyAdapter(plugin, this);

            m_pluginLibraryHandleMap[adapter] = handle;

            if (adapterFlags & ADAPT_INPUT_DOMAIN) {
                if (adapter->getInputDomain() == Plugin::FrequencyDomain) {
                    adapter = new PluginInputDomainAdapter(adapter);
                }
            }

            if (adapterFlags & ADAPT_CHANNEL_COUNT) {
                adapter = new PluginChannelAdapter(adapter);
            }

            return adapter;
        }

        ++index;
    }

    cerr << "Vamp::HostExt::PluginLoader: Plugin \""
         << identifier << "\" not found in library \""
         << fullPath << "\"" << endl;

    return 0;
}

void
PluginLoader::generateTaxonomy()
{
//    cerr << "PluginLoader::generateTaxonomy" << endl;

    vector<string> path = PluginHostAdapter::getPluginPath();
    string libfragment = "/lib/";
    vector<string> catpath;

    string suffix = "cat";

    for (vector<string>::iterator i = path.begin();
         i != path.end(); ++i) {

        // It doesn't matter that we're using literal forward-slash in
        // this bit, as it's only relevant if the path contains
        // "/lib/", which is only meaningful and only plausible on
        // systems with forward-slash delimiters
        
        string dir = *i;
        string::size_type li = dir.find(libfragment);

        if (li != string::npos) {
            catpath.push_back
                (dir.substr(0, li)
                 + "/share/"
                 + dir.substr(li + libfragment.length()));
        }

        catpath.push_back(dir);
    }

    char buffer[1024];

    for (vector<string>::iterator i = catpath.begin();
         i != catpath.end(); ++i) {
        
        vector<string> files = listFiles(*i, suffix);

        for (vector<string>::iterator fi = files.begin();
             fi != files.end(); ++fi) {

            string filepath = splicePath(*i, *fi);
            ifstream is(filepath.c_str(), ifstream::in | ifstream::binary);

            if (is.fail()) {
//                cerr << "failed to open: " << filepath << endl;
                continue;
            }

//            cerr << "opened: " << filepath << endl;

            while (!!is.getline(buffer, 1024)) {

                string line(buffer);

//                cerr << "line = " << line << endl;

                string::size_type di = line.find("::");
                if (di == string::npos) continue;

                string id = line.substr(0, di);
                string encodedCat = line.substr(di + 2);

                if (id.substr(0, 5) != "vamp:") continue;
                id = id.substr(5);

                while (encodedCat.length() >= 1 &&
                       encodedCat[encodedCat.length()-1] == '\r') {
                    encodedCat = encodedCat.substr(0, encodedCat.length()-1);
                }

//                cerr << "id = " << id << ", cat = " << encodedCat << endl;

                PluginCategoryHierarchy category;
                string::size_type ai;
                while ((ai = encodedCat.find(" > ")) != string::npos) {
                    category.push_back(encodedCat.substr(0, ai));
                    encodedCat = encodedCat.substr(ai + 3);
                }
                if (encodedCat != "") category.push_back(encodedCat);

                m_taxonomy[id] = category;
            }
        }
    }
}    

void *
PluginLoader::loadLibrary(string path)
{
    void *handle = 0;
#ifdef _WIN32
    handle = LoadLibrary(path.c_str());
    if (!handle) {
        cerr << "Vamp::HostExt::PluginLoader: Unable to load library \""
             << path << "\"" << endl;
    }
#else
    handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        cerr << "Vamp::HostExt::PluginLoader: Unable to load library \""
             << path << "\": " << dlerror() << endl;
    }
#endif
    return handle;
}

void
PluginLoader::unloadLibrary(void *handle)
{
#ifdef _WIN32
    FreeLibrary((HINSTANCE)handle);
#else
    dlclose(handle);
#endif
}

void *
PluginLoader::lookupInLibrary(void *handle, const char *symbol)
{
#ifdef _WIN32
    return (void *)GetProcAddress((HINSTANCE)handle, symbol);
#else
    return (void *)dlsym(handle, symbol);
#endif
}

string
PluginLoader::splicePath(string a, string b)
{
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

vector<string>
PluginLoader::listFiles(string dir, string extension)
{
    vector<string> files;
    size_t extlen = extension.length();

#ifdef _WIN32

    string expression = dir + "\\*." + extension;
    WIN32_FIND_DATA data;
    HANDLE fh = FindFirstFile(expression.c_str(), &data);
    if (fh == INVALID_HANDLE_VALUE) return files;

    bool ok = true;
    while (ok) {
        files.push_back(data.cFileName);
        ok = FindNextFile(fh, &data);
    }

    FindClose(fh);

#else
    DIR *d = opendir(dir.c_str());
    if (!d) return files;
            
    struct dirent *e = 0;
    while ((e = readdir(d))) {
        
        if (!(e->d_type & DT_REG) || !e->d_name) continue;
        
        size_t len = strlen(e->d_name);
        if (len < extlen + 2 ||
            e->d_name + len - extlen - 1 != "." + extension) {
            continue;
        }

        files.push_back(e->d_name);
    }

    closedir(d);
#endif

    return files;
}

void
PluginLoader::pluginDeleted(PluginDeletionNotifyAdapter *adapter)
{
    void *handle = m_pluginLibraryHandleMap[adapter];
    if (handle) unloadLibrary(handle);
    m_pluginLibraryHandleMap.erase(adapter);
}

PluginLoader::PluginDeletionNotifyAdapter::PluginDeletionNotifyAdapter(Plugin *plugin,
                                                                       PluginLoader *loader) :
    PluginWrapper(plugin),
    m_loader(loader)
{
}

PluginLoader::PluginDeletionNotifyAdapter::~PluginDeletionNotifyAdapter()
{
    // We need to delete the plugin before calling pluginDeleted, as
    // the delete call may require calling through to the descriptor
    // (for e.g. cleanup) but pluginDeleted may unload the required
    // library for the call.  To prevent a double deletion when our
    // parent's destructor runs (after this one), be sure to set
    // m_plugin to 0 after deletion.
    delete m_plugin;
    m_plugin = 0;

    if (m_loader) m_loader->pluginDeleted(this);
}

}

}
