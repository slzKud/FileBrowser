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

std::string BASE_DIR = "./files"; // 文件存储根目录
const std::string TMP_DIR = "./tmp";
unsigned int WEB_PORT = 8080;
const size_t DEFAULT_CHUNK_SIZE = 5 * 1024 * 1024; // 默认分块大小 5MB

// 创建存储目录
void init_storage()
{
    if (!fs::exists(BASE_DIR))
    {
        fs::create_directory(BASE_DIR);
    }
    if (!fs::exists(TMP_DIR))
    {
        fs::create_directories(TMP_DIR);
    }
}

// 生成随机ID
std::string generate_upload_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    const char *hex_chars = "0123456789abcdef";
    std::string id(32, ' ');
    for (int i = 0; i < 32; ++i)
    {
        id[i] = hex_chars[dis(gen)];
    }
    return id;
}

bool check_index_html(bool info = false)
{
    if (fs::exists("./index.html"))
    {
        if (info)
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
    // printf("resolve_path:%s %s\n", relative_path.c_str(), abs_path.lexically_normal().string().c_str());
    return abs_path.lexically_normal().string();
}

// 合并分块文件
bool merge_chunks(const std::string &upload_id, const std::string &filename,
                  int total_chunks, const std::string &target_path)
{
    std::ofstream out_file(target_path, std::ios::binary | std::ios::app);
    if (!out_file)
    {
        return false;
    }

    for (int i = 0; i < total_chunks; i++)
    {
        std::string chunk_file = TMP_DIR + "/" + upload_id + "_" + std::to_string(i);

        if (!fs::exists(chunk_file))
        {
            out_file.close();
            fs::remove(target_path);
            return false;
        }

        std::ifstream in_file(chunk_file, std::ios::binary);
        if (!in_file)
        {
            out_file.close();
            fs::remove(target_path);
            return false;
        }

        out_file << in_file.rdbuf();
        in_file.close();

        // 删除临时块文件
        fs::remove(chunk_file);
    }

    out_file.close();
    return true;
}

bool parse_cmd_arg(int argc, char *argv[])
{
    int opt;
    char string[] = "d:hp:";
    while ((opt = getopt(argc, argv, string)) != -1)
    {
        if (opt == 'h')
        {
            printf("fileBrowser V1.0 \nA simple web file Browser\nUsage: %s [-d root_dir] [-p port] [-h]\n\t -d : set root path.\n\t -p : set server port.\n\t -h : show helps.\n", argv[0]);
            return true;
        }
        if (opt == 'd')
        {
            BASE_DIR = optarg;
        }
        if (opt == 'p')
        {
            sscanf(optarg, "%d", &WEB_PORT);
            if (WEB_PORT == 0)
                WEB_PORT = 8080;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    if (parse_cmd_arg(argc, argv))
    {
        return 0;
    }
    std::cout << "Root Path:" << BASE_DIR << "\n";
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
                } });

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

    // 分块上传处理
    svr.Post("/upload", [](const Request &req, Response &res)
             {
    // 获取表单字段
    std::string path = req.form.get_field("path");
    std::string filename = req.form.get_field("filename");
    std::string upload_id = req.form.get_field("upload_id");
    int chunk_index = -1;
    int total_chunks = -1;
    
    try {
        chunk_index = std::stoi(req.form.get_field("chunk_index"));
        total_chunks = std::stoi(req.form.get_field("total_chunks"));
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid chunk parameters\"}", "application/json");
        return;
    }
    
    // 验证参数
    if (filename.empty() || chunk_index < 0 || total_chunks <= 0 || chunk_index >= total_chunks) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid parameters\"}", "application/json");
        return;
    }
    
    // 处理上传ID（首次上传时生成）
    if (upload_id.empty()) {
        upload_id = generate_upload_id();
    }
    
    // 获取目标路径
    std::string abs_path = resolve_path(path);
    if (!fs::exists(abs_path) || !fs::is_directory(abs_path)) {
        res.status = 404;
        res.set_content("{\"error\":\"Directory not found\"}", "application/json");
        return;
    }
    
    // 获取文件块
    auto file = req.form.get_file("file");
    if (file.filename.empty() || file.content.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"No file data\"}", "application/json");
        return;
    }
    
    // 保存块文件
    std::string chunk_filename = TMP_DIR + "/" + upload_id + "_" + std::to_string(chunk_index);
    std::ofstream chunk_file(chunk_filename, std::ios::binary);
    if (!chunk_file) {
        res.status = 500;
        res.set_content("{\"error\":\"Failed to save chunk\"}", "application/json");
        return;
    }
    
    chunk_file.write(file.content.data(), file.content.size());
    chunk_file.close();
    
    // 检查是否所有块都已上传
    bool all_chunks_uploaded = true;
    for (int i = 0; i < total_chunks; i++) {
        std::string test_chunk = TMP_DIR + "/" + upload_id + "_" + std::to_string(i);
        if (!fs::exists(test_chunk)) {
            all_chunks_uploaded = false;
            break;
        }
    }
    
    // 如果所有块都已上传，合并文件
    std::string final_path;
    if (all_chunks_uploaded) {
        final_path = abs_path + "/" + filename;
        if (fs::exists(final_path)) {
            // 如果文件已存在，添加后缀
            int counter = 1;
            std::string base_name = filename;
            std::string ext = "";
            
            size_t dot_pos = filename.find_last_of('.');
            if (dot_pos != std::string::npos) {
                base_name = filename.substr(0, dot_pos);
                ext = filename.substr(dot_pos);
            }
            
            do {
                std::ostringstream oss;
                oss << base_name << " (" << counter << ")" << ext;
                final_path = abs_path + "/" + oss.str();
                counter++;
            } while (fs::exists(final_path));
        }
        
        if (!merge_chunks(upload_id, filename, total_chunks, final_path)) {
            res.status = 500;
            res.set_content("{\"error\":\"Failed to merge chunks\"}", "application/json");
            return;
        }
        
        // 返回成功响应
        res.set_content(
            "{\"status\":\"completed\",\"upload_id\":\"" + upload_id + 
            "\",\"filename\":\"" + fs::path(final_path).filename().string() + "\"}", 
            "application/json"
        );
    } else {
        // 返回部分上传成功响应
        res.set_content(
            "{\"status\":\"partial\",\"upload_id\":\"" + upload_id + 
            "\",\"chunk_index\":" + std::to_string(chunk_index) + 
            ",\"total_chunks\":" + std::to_string(total_chunks) + "}", 
            "application/json"
        );
    } });

    // 获取上传进度
    svr.Get("/upload/progress", [](const Request &req, Response &res)
            {
    std::string upload_id = req.get_param_value("upload_id");
    int total_chunks = -1;
    
    try {
        total_chunks = std::stoi(req.get_param_value("total_chunks"));
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid parameters\"}", "application/json");
        return;
    }
    
    if (upload_id.empty() || total_chunks <= 0) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid parameters\"}", "application/json");
        return;
    }
    
    int uploaded_chunks = 0;
    for (int i = 0; i < total_chunks; i++) {
        std::string chunk_file = TMP_DIR + "/" + upload_id + "_" + std::to_string(i);
        if (fs::exists(chunk_file)) {
            uploaded_chunks++;
        }
    }
    
    int progress = static_cast<int>((static_cast<double>(uploaded_chunks) / total_chunks * 100));
    
    res.set_content(
        "{\"upload_id\":\"" + upload_id + 
        "\",\"uploaded_chunks\":" + std::to_string(uploaded_chunks) + 
        ",\"total_chunks\":" + std::to_string(total_chunks) + 
        ",\"progress\":" + std::to_string(progress) + "}", 
        "application/json"
    ); });

    // 取消上传
    svr.Post("/upload/cancel", [](const Request &req, Response &res)
             {
    std::string upload_id = req.get_param_value("upload_id");
    int total_chunks = -1;
    
    try {
        total_chunks = std::stoi(req.get_param_value("total_chunks"));
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid parameters\"}", "application/json");
        return;
    }
    
    if (upload_id.empty() || total_chunks <= 0) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid parameters\"}", "application/json");
        return;
    }
    
    // 删除所有块文件
    for (int i = 0; i < total_chunks; i++) {
        std::string chunk_file = TMP_DIR + "/" + upload_id + "_" + std::to_string(i);
        if (fs::exists(chunk_file)) {
            fs::remove(chunk_file);
        }
    }
    
    res.set_content("{\"status\":\"cancelled\"}", "application/json"); });

    // 文件下载
    svr.Get("/download", [](const Request &req, Response &res)
            {
                std::string rel_path = req.get_param_value("path");
                std::string filename = req.get_param_value("filename");
                std::string abs_path = resolve_path(rel_path + "/" + filename);
                if (!std::ifstream(abs_path, std::ifstream::binary | std::ifstream::in).good()) {
                    res.status = 404;
                    res.set_content("404 not found", "text/plain; charset=UTF-8");
                    return;
                }
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
                res.set_chunked_content_provider("application/octet-stream", [abs_path](size_t offset, httplib::DataSink& sink) {
                    // open file
                    std::ifstream file_reader(abs_path, std::ifstream::binary | std::ifstream::in);

                    // can't open file, cancel process
                    if (!file_reader.good())
                        return false;

                    // get file size
                    file_reader.seekg(0, file_reader.end);
                    size_t file_size = file_reader.tellg();
                    file_reader.seekg(0, file_reader.beg);

                    // check offset and file size, cancel process if offset >= file_size
                    if (offset >= file_size)
                        return false;
                    
                    // larger chunk size get faster download speed, more memory usage, more bandwidth usage, more disk I/O usage
                    const size_t chunk_size = DEFAULT_CHUNK_SIZE / 1024;

                    // prepare read size of chunk
                    size_t read_size = 0;
                    bool last_chunk = false;
                    if (file_size - offset > chunk_size) {
                        read_size = chunk_size;
                        last_chunk = false;
                    } else {
                        read_size = file_size - offset;
                        last_chunk = true;
                    }

                    // allocate temp buffer, and read file chunk into buffer
                    std::vector<char> buffer;
                    buffer.reserve(chunk_size);
                    file_reader.seekg(offset, file_reader.beg);
                    file_reader.read(&buffer[0], read_size);
                    file_reader.close();

                    // write buffer to sink
                    sink.write(&buffer[0], read_size);

                    // done after last chunk had write to sink
                    if (last_chunk)
                        sink.done();

                    return true;
                }); 
        });

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

    std::cout << "File Browser running on http://localhost:" << WEB_PORT << "\n";
    svr.listen("0.0.0.0", WEB_PORT);
    return 0;
}