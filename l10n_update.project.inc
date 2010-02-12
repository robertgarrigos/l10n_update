<?php
// $Id$
/**
 * @file
 * Library for querying Drupal projects
 * 
 * Most code is taken from update module. We don't want to depend on it though as it may not be enabled. 
 * 
 * For each project, the information about where to fetch translations may be specified in the info files
 * as follows:
 * 
 * - Localization server to be used for this project. Defaults to http://localize.drupal.org
 *   l10n server = http://localize.drupal.org
 *   
 * - Translation file URL template, will be used to build the file url to download
 *   l10n path = http://ftp.drupal.org/files/translations/%core/%project/%project-%release.%language.po
 *   (Alternatively you can use the %filename variable that will default to '%project-%release.%language.po')
 */

/**
 * Rebuild project list
 */
function l10n_update_build_projects() {
  module_load_include('inc', 'l10n_update');
  // Get all stored projects, including disabled ones
  $current = l10n_update_get_projects(NULL, TRUE);
  // Now get the new project list, just enabled ones
  $projects = l10n_update_project_list();
  // Mark all previous projects as disabled and store new project data
  db_query("UPDATE {l10n_update_project} SET status = 0");
  $default_server = l10n_update_default_server();
  foreach ($projects as $name => $data) {
    $data += array(
      'version' => isset($data['info']['version']) ? $data['info']['version'] : '',
      'core' => isset($data['info']['core']) ? $data['info']['core'] : DRUPAL_CORE_COMPATIBILITY,
      // The project can have its own l10n server, we use default if not
      'l10n_server' => isset($data['info']['l10n server']) ? $data['info']['l10n server'] : $default_server['name'],
      // A project can provide the server url to fetch metadata, or the update url (path)
      'l10n_url' => isset($data['info']['l10n url']) ? $data['info']['l10n url'] : NULL,
      'l10n_path' => isset($data['info']['l10n path']) ? $data['info']['l10n path'] : NULL,
      'status' => 1,
    );
    $project = (object)$data;
    // Unless the project provides a full l10n path (update url), we try to build one
    if (!isset($project->l10n_path) && ($server = l10n_update_server($project->l10n_server, $project->l10n_url))) {
      $project->l10n_path = l10n_update_build_string($project, $server['update_url']);
    }
    // Create / update project record
    $update = empty($current[$name]) ? array() : array('name');
    drupal_write_record('l10n_update_project', $project, $update);
    $projects[$name] = $project;
  }
  return $projects;
}

/**
 * Get update module's project list
 */
function l10n_update_project_list() {
  module_load_include('compare.inc', 'update');  
  $projects = array();
  // Unlike update module, this one has no cache
  _update_process_info_list($projects, module_rebuild_cache(), 'module');
  _update_process_info_list($projects, system_theme_data(), 'theme');
  // Allow other modules to alter projects before fetching and comparing.
  drupal_alter('update_projects', $projects);
  // Provide an aditional alter hook, l10n_update specific
  drupal_alter('l10n_update_projects', $projects);
  return $projects;
}

/**
 * Refresh projects after enabling/disabling modules
 * 
 * When new projects are installed, set a batch for locale import / update
 */
function l10n_update_project_refresh() {
  // Get previous enabled/disabled projects
  $prev_enabled = l10n_update_get_projects();
  $prev_all = l10n_update_get_projects(FALSE, TRUE);
  // Get all current projects, also rebuild table data
  $current_projects = l10n_update_build_projects();
  $enabled = array_diff(array_keys($current_projects), array_keys($prev_enabled));
  $installed = array_diff(array_keys($current_projects), array_keys($prev_all));
  // If there are newly installed projects, create batch for projects x languages
  if ($installed) {
    module_load_include('check.inc', 'l10n_update');
    foreach ($installed as $name) {
      $projects[$name] = $current_projects[$name];
    }
    if ($available = l10n_update_check_projects($projects)) {
      $history = l10n_update_get_history();
      if ($updates = l10n_update_build_updates($history, $available)) {
        module_load_include('batch.inc', 'l10n_update');
        // Filter out updates in other languages. If no languages, all of them will be updated
        $updates = _l10n_update_prepare_updates($updates); 
        $batch = l10n_update_batch_multiple($updates, variable_get('l10n_update_import_mode', LOCALE_IMPORT_KEEP));
        batch_set($batch);        
      }
    }
  }
}

/***
 * Get available updates for a project
 */
function l10n_update_project_updates($project) {
  
}