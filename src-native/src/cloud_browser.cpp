#include "cloud_browser.hpp"
#include "logger.hpp"
#include "sync_job_metadata.hpp"
#include "device_identity.hpp"
#include "file_index.hpp"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <array>
#include <memory>
#include <regex>
#include <ctime>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

// ============================================================================
// NATIVE UI MODE STUBS
// In native GTK4 mode, cloud browsing is handled via AppWindow
// CloudBrowser provides minimal functionality
// ============================================================================

CloudBrowser& CloudBrowser::getInstance() {
    static CloudBrowser instance;
    return instance;
}

GtkWidget* CloudBrowser::get_widget() {
    return nullptr; // No widget in native mode
}

void CloudBrowser::build_ui() {
    // No-op in native mode
}

void CloudBrowser::populate_tree(const std::vector<CloudItem>& items) {
    (void)items;
}

void CloudBrowser::on_row_activated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn* col) {
    (void)tree; (void)path; (void)col;
}

void CloudBrowser::upload_files_to_cloud(const std::vector<std::string>& files, 
                                          const std::string& target_folder) {
    Logger::info("[CloudBrowser] Upload files requested to: " + target_folder);
    (void)files;
}

std::vector<CloudItem> CloudBrowser::list_remote(const std::string& path) {
    (void)path;
    return {};
}

std::vector<CloudItem> CloudBrowser::convert_indexed_to_cloud_items(const std::vector<IndexedFile>& indexed_files) {
    std::vector<CloudItem> items;
    for (const auto& f : indexed_files) {
        CloudItem item;
        item.name = f.name;
        item.path = f.path;
        item.is_directory = f.is_directory;
        item.size = f.size;
        item.mod_time = f.mod_time;
        items.push_back(item);
    }
    return items;
}

bool CloudBrowser::is_path_synced(const std::string& remote_path) {
    (void)remote_path;
    return false;
}

std::string CloudBrowser::get_local_sync_path(const std::string& remote_path) {
    (void)remote_path;
    return "";
}

void CloudBrowser::navigate_to(const std::string& path) {
    Logger::info("[CloudBrowser] Navigate to: " + path);
}

void CloudBrowser::go_home() {
    Logger::info("[CloudBrowser] Go home");
}

void CloudBrowser::go_up() {
    Logger::info("[CloudBrowser] Go up");
}

void CloudBrowser::refresh() {
    Logger::info("[CloudBrowser] Refresh");
}

void CloudBrowser::refresh_async() {
    Logger::info("[CloudBrowser] Refresh async");
}

void CloudBrowser::force_refresh_from_cloud() {
    Logger::info("[CloudBrowser] Force refresh from cloud");
}

void CloudBrowser::sync_selected_items() {
    Logger::info("[CloudBrowser] Sync selected items");
}

void CloudBrowser::unsync_item(const std::string& path) {
    Logger::info("[CloudBrowser] Unsync item: " + path);
}

void CloudBrowser::sync_to_cloud(const std::string& remote_path, const std::string& local_path) {
    Logger::info("[CloudBrowser] Sync to cloud: " + remote_path + " -> " + local_path);
}

void CloudBrowser::include_item(const std::string& job_id, const std::string& exclude_path, const std::string& item_path) {
    (void)job_id; (void)exclude_path; (void)item_path;
}

void CloudBrowser::remove_local_copy(const std::string& remote_path, const std::string& local_path) {
    (void)remote_path; (void)local_path;
}

void CloudBrowser::download_item(const std::string& remote_path, const std::string& local_path) {
    Logger::info("[CloudBrowser] Download: " + remote_path + " to " + local_path);
}

void CloudBrowser::open_file(const CloudItem& item) {
    Logger::info("[CloudBrowser] Open file: " + item.name);
}

void CloudBrowser::move_to_trash(const std::string& path) {
    Logger::info("[CloudBrowser] Move to trash: " + path);
}

void CloudBrowser::restore_from_trash(const std::string& path) {
    Logger::info("[CloudBrowser] Restore from trash: " + path);
}

void CloudBrowser::empty_trash() {
    Logger::info("[CloudBrowser] Empty trash");
}

std::vector<CloudItem> CloudBrowser::get_trash_contents() {
    return {};
}

void CloudBrowser::search_files(const std::string& query) {
    Logger::info("[CloudBrowser] Search files: " + query);
}

void CloudBrowser::clear_search() {
    // No-op
}

void CloudBrowser::start_indexing() {
    Logger::info("[CloudBrowser] Start indexing");
    auto& index = FileIndex::getInstance();
    index.start_background_index();
}

void CloudBrowser::perform_search(const std::string& query) {
    (void)query;
}

void CloudBrowser::show_search_results(const std::vector<IndexedFile>& results) {
    (void)results;
}

void CloudBrowser::on_search_result_activated(const IndexedFile& file) {
    (void)file;
}

