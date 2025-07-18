#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "httplib.h"
#include <algorithm>
#include <cctype>

#include "base64.h"
#include "html.cc"

namespace fs = std::filesystem;
using namespace httplib;

std::string BASE_DIR = "./files";     // 文件存储根目录
unsigned int WEB_PORT = 8080;
const size_t CHUNK_SIZE = 10 * 1024 * 1024; // 10MB分块大小


// 创建存储目录
void init_storage()
{
    if (!fs::exists(BASE_DIR))
    {
        fs::create_directory(BASE_DIR);
    }
}

bool check_index_html(bool info=false)
{
    if (fs::exists("./index.html"))
    {
        if(info)
            std::cout << "External index html exists, it will use.\n";
        return true;
    }
    return false;
}

// 安全获取路径（防止路径遍历攻击）
std::string sanitize_path(const std::string &path)
{
    fs::path p(path);
    fs::path sanitized;

    for (const auto &part : p)
    {
        if (part == "..")
            continue;
        sanitized /= part;
    }

    return sanitized.lexically_normal().string();
}

// 获取当前路径的绝对路径
std::string resolve_path(const std::string &relative_path)
{
    std::string sanitized = sanitize_path(relative_path);
    fs::path abs_path = fs::absolute(BASE_DIR) / sanitized;
    //printf("resolve_path:%s %s\n", relative_path.c_str(), abs_path.lexically_normal().string().c_str());
    return abs_path.lexically_normal().string();
}

bool parse_cmd_arg(int argc, char* argv[]){
    int opt;
    char string[] = "d:hp:";
    while ((opt = getopt(argc, argv, string))!= -1)
    {  
        if(opt=='h'){
            printf("fileBrowser V1.0 \nA simple web file Browser\nUsage: %s [-d root_dir] [-p port] [-h]\n\t -d : set root path.\n\t -p : set server port.\n\t -h : show helps.\n",argv[0]);
            return true;
        }
        if(opt=='d'){
            BASE_DIR=optarg;
        }
        if(opt=='p'){
            sscanf(optarg,"%d",&WEB_PORT);
            if(WEB_PORT==0)
                WEB_PORT=8080;
        }
    }  
    return false;
}

int main(int argc, char* argv[])
{
    if(parse_cmd_arg(argc,argv)){
        return 0;
    }
    std::cout << "Root Path:" << BASE_DIR <<"\n";
    init_storage();
    check_index_html(true);
    Server svr;
    
    // 前端页面
    svr.Get("/", [](const Request &req, Response &res)
            { 
                if (!check_index_html()){
                    std::string FRONTEND_HTML = base64_decode(FRONTEND_HTML_BASE64);
                    res.set_content(FRONTEND_HTML, "text/html"); 
                }else{
                    res.set_file_content("./index.html","text/html");
                }
            }
        );

    // 文件浏览接口（支持文件夹）
    svr.Get("/browse", [](const Request &req, Response &res)
            {
        std::string rel_path = req.get_param_value("path");
        std::string abs_path = resolve_path(rel_path);
        
        if (!fs::exists(abs_path) || !fs::is_directory(abs_path)) {
            res.status = 404;
            return;
        }
        
        std::vector<std::unordered_map<std::string, std::string>> entries;
        
        // 添加父目录链接（如果不是根目录）
        if (rel_path != "/" && !rel_path.empty()) {
            entries.push_back({
                {"name", ".."},
                {"type", "directory"},
                {"size", "0"}
            });
        }
        
        // 遍历目录
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            std::string name = entry.path().filename().string();
            bool is_dir = fs::is_directory(entry.path());
            
            entries.push_back({
                {"name", name},
                {"type", is_dir ? "directory" : "file"},
                {"size", std::to_string(is_dir ? 0 : fs::file_size(entry.path()))}
            });
        }
        
        // 排序：文件夹在前，按字母顺序
        std::sort(entries.begin(), entries.end(), 
            [](const auto& a, const auto& b) {
                if (a.at("type") == "directory" && b.at("type") != "directory") return true;
                if (a.at("type") != "directory" && b.at("type") == "directory") return false;
                return a.at("name") < b.at("name");
            }
        );
        
        // 生成JSON
        std::string json = "[";
        for (size_t i = 0; i < entries.size(); i++) {
            json += "{";
            for (const auto& [key, value] : entries[i]) {
                json += "\"" + key + "\":\"" + value + "\",";
            }
            json.pop_back(); // 移除最后一个逗号
            json += "}";
            if (i < entries.size() - 1) json += ",";
        }
        json += "]";
        
        res.set_content(json, "application/json"); });

    // 大文件分块上传
    svr.Post("/upload", [](const Request &req, Response &res)
             {
        std::string rel_path = req.form.get_field("path");
        std::string abs_path = resolve_path(rel_path);
        
        if (!fs::exists(abs_path) || !fs::is_directory(abs_path)) {
            res.status = 404;
            res.set_content("{\"error\":\"Directory not found\"}", "application/json");
            return;
        }
        
        auto file = req.form.get_file("file");
        std::string filename = file.filename;
        std::string path = abs_path + "/" + filename;
        
        std::ofstream ofs(path, std::ios::binary | std::ios::app);
        if (!ofs) {
            res.status = 500;
            res.set_content("{\"error\":\"File creation failed\"}", "application/json");
            return;
        }
        
        ofs.write(file.content.data(), file.content.size());
        res.set_content("{\"status\":\"success\"}", "application/json"); });

    // 文件下载（支持Range头）
    svr.Get("/download", [](const Request &req, Response &res)
            {
        std::string rel_path = req.get_param_value("path");
        std::string filename = req.get_param_value("filename");
        std::string abs_path = resolve_path(rel_path + "/" + filename);
        res.set_file_content(abs_path,"application/octet-stream"); });

    // 文件删除
    svr.Post("/delete", [](const Request &req, Response &res)
             {
        std::string rel_path = req.get_param_value("path");
        std::string filename = req.get_param_value("name");
        std::string abs_path = resolve_path(rel_path + "/" + filename);
        
        if (!fs::exists(abs_path)) {
            res.status = 404;
            return;
        }
        
        if (fs::remove(abs_path)) {
            res.set_content("{\"status\":\"success\"}", "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"error\":\"Deletion failed\"}", "application/json");
        } });

    // 文件重命名
    svr.Post("/rename", [](const Request &req, Response &res)
             {
        std::string rel_path = req.get_param_value("path");
        std::string oldname = req.get_param_value("oldname");
        std::string newname = req.get_param_value("newname");
        
        std::string oldpath = resolve_path(rel_path + "/" + oldname);
        std::string newpath = resolve_path(rel_path + "/" + newname);
        
        if (!fs::exists(oldpath)) {
            res.status = 404;
            return;
        }
        
        try {
            fs::rename(oldpath, newpath);
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (...) {
            res.status = 500;
            res.set_content("{\"error\":\"Rename failed\"}", "application/json");
        } });

    // 文件夹创建
    svr.Post("/mkdir", [](const Request &req, Response &res)
             {
        std::string rel_path = req.get_param_value("path");
        std::string dirname = req.get_param_value("dirname");
        std::string abs_path = resolve_path(rel_path + "/" + dirname);
        
        if (fs::exists(abs_path)) {
            res.status = 400;
            res.set_content("{\"error\":\"Directory already exists\"}", "application/json");
            return;
        }
        
        if (fs::create_directory(abs_path)) {
            res.set_content("{\"status\":\"success\"}", "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"error\":\"Failed to create directory\"}", "application/json");
        } });

    // 文件夹删除
    svr.Post("/delete_folder", [](const Request &req, Response &res)
             {
        std::string rel_path = req.get_param_value("path");
        std::string dirname = req.get_param_value("name");
        std::string abs_path = resolve_path(rel_path + "/" + dirname);
        
        if (!fs::exists(abs_path) || !fs::is_directory(abs_path)) {
            res.status = 404;
            return;
        }
        
        if (fs::remove_all(abs_path) > 0) {
            res.set_content("{\"status\":\"success\"}", "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"error\":\"Deletion failed\"}", "application/json");
        } });

    std::cout << "File Browser running on http://localhost:"<< WEB_PORT <<"\n";
    svr.listen("0.0.0.0", WEB_PORT);
    return 0;
}