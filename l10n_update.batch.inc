<?php 
// $Id$
/**
 * @file
 *   Reusable API for creating and running l10n update batches
 */
include_once 'l10n_update.inc';

/**
 * Create a batch to download and import
 */
function l10n_update_batch_download($url, $locale, $mode = LOCALE_IMPORT_OVERWRITE) {  
  $operations[] = array('_l10n_update_batch_download_import', array($url, $locale, $mode));
  //$operations[] = array('_l10n_update_batch_import', array($locale));
  return _l10n_update_create_batch($operations);
}

/**
 * Create a big batch for multiple projects and languages
 * 
 * @param $projects
 *   Array of projects
 * @param $languages
 *   Array of language codes
 * @param $updates
 *   Optional array of available updates. will be calculated if not passed
 */
function l10n_update_batch_multiple($projects, $languages, $updates = NULL, $mode = LOCALE_IMPORT_OVERWRITE) {
  if (is_null($updates)) {
    $available = _l10n_update_fetch_releases($projects, $languages);
    $updates = _l10n_update_compare($projects, array(), $available);
  }
  foreach ($projects as $project) {
    foreach ($languages as $lang) {
      if (!empty($updates[$project['name']][$lang])) {
        $release = $updates[$project['name']][$lang];
        $operations[] = array('_l10n_update_batch_download_import', array($release['download_link'], $lang, $mode));
        $operations[] = array('_l10n_update_batch_history', array($project, $release));
      }
    }
  }
  if (!empty($operations)) {
    return _l10n_update_create_batch($operations);
  }
}

/**
 * Create batch stub for this module
 * 
 * @return $batch
 */
function _l10n_update_create_batch($operations = array()) {
  $t = get_t();
  $batch = array(
    'operations'    => $operations,
    'title'         => $t('Updating translation.'),
    'init_message'  => $t('Downloading and importing files.'),
    'error_message' => $t('Error importing interface translations'),
    'file'          => drupal_get_path('module', 'l10n_update') . '/l10n_batch.inc',
    'finished'      => '_l10n_update_batch_finished',
  );
  return $batch;
}

/**
 * Some complex batch callback to handle the full download and import
 * 
 * Done this way so we pass variables from one step to the next
 * and we can better handle errors
 * 
 * I love batch processes ;-)
 */
function _l10n_update_batch_download_import($url, $locale, $mode, &$context) {
  $t = get_t();
  if (!isset($context['sandbox']['step'])) {
    $context['sandbox']['step'] = 0;
  }

  switch ($context['sandbox']['step']) {
    case 0: // Download message (for multiple downloads batch)
      // This message may never show up if next step takes less than a second?
      $context['message'] = $t('Downloading remote file from %url', array('%url' => $url));
      $context['sandbox']['step'] = 1;
      $context['finished'] = 0.1;
      break;
    case 1: // Download      
      if ($file = l10n_update_download_file($url)) {
        $context['sandbox']['file'] = $file;
        $context['message'] = $t('Importing downloaded translation: %url.', array('%url' => $url));
        $context['sandbox']['step'] = 2;
        $context['results'][] = $url;
      }
      else {
        $context['sandbox']['step'] = 10;
      }
      $context['finished'] = 0.5;      
      break;
    case 2: // Import
      if (!empty($context['sandbox']['file'])) {
        $file = $context['sandbox']['file'];
        $context['results'][] = $file;
        if (l10n_update_import_file($file, $locale, $mode)) {
          $context['message'] = $t('Imported translation file.');
          $context['finished'] = 1;
          drupal_set_message($t('Successfully downloaded and imported translation from %url', array('%url' => $url)));
        }
        else {
          $context['sandbox']['step'] = 10;
        }
        file_delete($file);
      }
      else {
        // This should not happen, just in case
        $context['sandbox']['step'] = 10;
      }
      
      break;    
    case 10: // Error
    default: // In case something goes really wrong
      $context['finished'] = 1;
      drupal_set_message($t('The download and import operation failed: %url', array('%url' => $url)), 'error');
      break;
  }      
}

function _l10n_update_batch_download($url, &$context) {
  $t = get_t();
  if ($file = l10n_update_download_file($url)) {
    $context['results'][] = $file;
  }
  else {
    drupal_set_message($t('Failed download of %url', array('%url' => $url)), 'error');
  }
}

/**
 * Batch process: Update the download history table
 */
function _l10n_update_batch_history($project, $release, &$context) {
  l10n_update_download_history($project, $release);
}

/**
 * Batch import translation file
 * 
 * This takes a file parameter or continues from previous batch which should have downloaded a file
 * 
 * @param $file
 *   File to be imported. If empty, the file will be taken from $context['results']
 * @param $locale
 * @param $mode
 * @param $context
 */
function _l10n_update_batch_import($file, $locale, $mode, &$context) {
  $t = get_t();
  
  if (!$file && !empty($context['results'])) {
    $file = array_shift($context['results']);
  }
  if ($file) {
    if (l10n_update_import_file($file, $locale, $mode)) {
      drupal_set_message($t('Imported translation file %name.', array('%name' => $file->filepath)));
      file_delete($file);
    }
    else {
      drupal_set_message($t('Failed import of translation file %name.', array('%name' => $file->filepath)), 'error');
    }
  }
}

/**
 * Batch finished callback, set result message
 * 
 * @param $success
 * @param $results
 * @return unknown_type
 */
function _l10n_update_batch_finished($success, $results) {
  $t = get_t();
  if ($success) {
    drupal_set_message($t('Successfully imported translations.'));
  }
  else {
    drupal_set_message($t('Error importing translations.'), 'error');
  }
}