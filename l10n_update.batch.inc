<?php 
// $Id$
/**
 * @file
 *   Reusable API for creating and running l10n update batches
 */
include_once 'l10n_update.check.inc';

/**
 * Create a batch to just download files
 * 
 * All sources must have a 'fileurl'
 */
function l10n_update_batch_download($sources) {  
  foreach ($updates as $update) {
    $operations[] = array('_l10n_update_batch_download', array($update));
  }
  return _l10n_update_create_batch($operations);
}

/**
 * Create a batch to just import files
 * 
 * All sources must have a 'filepath'
 */
function l10n_update_batch_import($sources, $mode = LOCALE_IMPORT_OVERWRITE) {
  foreach ($updates as $update) {
    $operations[] = array('_l10n_update_batch_import', array($update, $mode));
  }
  return _l10n_update_create_batch($operations);
}

/**
 * Create a big batch for multiple projects and languages
 * 
 * @param $updates
 *   Array of update sources to be run
 * @param $mode
 *   Import mode
 */
function l10n_update_batch_multiple($updates, $mode = LOCALE_IMPORT_OVERWRITE) {
  foreach ($updates as $update) {
    if ($update->type == 'download') {
      $operations[] = array('_l10n_update_batch_download', array($update));
      $operations[] = array('_l10n_update_batch_import', array(NULL, $mode));
    }
    else {
      $operations[] = array('_l10n_update_batch_import', array($update, $mode));
    }
    // This one takes always parameters from results
    $operations[] = array('_l10n_update_batch_history', array(NULL));
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
    'file'          => drupal_get_path('module', 'l10n_update') . '/l10n_update.batch.inc',
    'finished'      => '_l10n_update_batch_finished',
  );
  return $batch;
}


/**
 * Batch download file
 * 
 * @param $update
 * @param $context
 * @return unknown_type
 */
function _l10n_update_batch_download($update, &$context) {
  $t = get_t();
  if (l10n_update_source_download($update)) {
    $context['message'] = $t('Importing downloaded translation: %url.', array('%url' => $update->fileurl));
    $context['results'][] = $update;
  }
  else {
    drupal_set_message($t('Failed download from %url', array('%url' => $update->fileurl)), 'error');
  }  
}

/**
 * Batch process: Update the download history table
 */
function _l10n_update_batch_history($update, &$context) {
  if ($update = _l10n_update_batch_param($update, $context)) {
    l10n_update_source_history($update);
    $context['results'][] = $update;
  }
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
function _l10n_update_batch_import($file, $mode, &$context) {
  $t = get_t();
  if ($file = _l10n_update_batch_param($file, $context)) {
    if (l10n_update_source_import($file, $mode)) {
      $context['results'][] = $file;
      if ($file->type == 'download') {
        drupal_set_message($t('Successfully downloaded and imported translation from %url', array('%url' => $update->fileurl)));        
      }
      else {
        drupal_set_message($t('Imported translation file %name.', array('%name' => $file->filepath)));
      }
    }
    else {
      drupal_set_message($t('Failed import of translation file %name.', array('%name' => $file->filepath)), 'error');
    }
  }
}

/**
 * Get parameter from results of previous batch if not present
 */
function _l10n_update_batch_param($param, &$context) {
  if (isset($param)) {
    return $param;
  }
  elseif (!empty($context['results'])) {
    return array_pop($context['results']);
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

/**
 * Some complex batch callback to handle the full download and import
 * 
 * Done this way so we pass variables from one step to the next
 * and we can better handle errors
 * 
 * I love batch processes ;-)
 */
function _l10n_update_batch_download_import($update, $mode, &$context) {
  $t = get_t();
  if (!isset($context['sandbox']['step'])) {
    $context['sandbox']['step'] = 0;
    $context['sandbox']['update'] = $update;
  }
  else {
    $update = $context['sandbox']['update']; 
  }
  switch ($context['sandbox']['step']) {
    case 0: // Download message (for multiple downloads batch)
      // This message may never show up if next step takes less than a second?
      $context['message'] = $t('Downloading remote file from %url', array('%url' => $update->fileurl));
      $context['sandbox']['step'] = 1;
      $context['finished'] = 0.1;
      break;
    case 1: // Download      
      if ($file = l10n_update_download_file($update->fileurl)) {
        $update->filepath = $file;
        $context['sandbox']['file'] = $file;
        $context['message'] = $t('Importing downloaded translation: %url.', array('%url' => $update->fileurl));
        $context['sandbox']['step'] = 2;
        $context['results'][] = $url;
      }
      else {
        $context['sandbox']['step'] = 10;
      }
      $context['finished'] = 0.5;
      break;
    case 2: // Import
      if (!empty($update->filepath)) {
        if (l10n_update_import_file($update->filepath, $update->language, $mode)) {
          $update->import_date = time();
          $context['message'] = $t('Imported translation file.');
          $context['finished'] = 0.9;
          $context['sandbox']['step'] = 3;
          drupal_set_message($t('Successfully downloaded and imported translation from %url', array('%url' => $update->fileurl)));
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
    case 3: // Update history
      if (!empty($update->import_time)) {
        l10n_update_file_history($update);
        $context['finished'] = 1;
      }
      else {
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
