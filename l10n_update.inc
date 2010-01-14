<?php 
// $Id$
/**
 * @file
 *   Reusable API for l10n remote updates
 */

include_once './includes/locale.inc';
include_once 'l10n_update.locale.inc';

// Translation status: String imported from po
define('L10N_UPDATE_STRING_DEFAULT', 0);
// Translation status: Custom string, overridden original import
define('L10N_UPDATE_STRING_CUSTOM', 1);

/**
 * Default update server, filename and URL
 */
define('L10N_UPDATE_DEFAULT_SERVER', 'http://localize.drupal.org');
define('L10N_UPDATE_DEFAULT_FILENAME', '%project-%release.%language.po');
define('L10N_UPDATE_DEFAULT_URL', 'http://ftp.drupal.org/files/translations/%core/%project/%filename');

/**
 * Download and import remote file
 */
function l10n_update_download_import($download_url, $locale, $mode = LOCALE_IMPORT_OVERWRITE) {
  if ($file = l10n_update_download_file($download_url)) {
    $result = l10n_update_import_file($file, $locale, $mode);
    file_delete($file);
    return $result;
  }
}

/**
 * Import local file into the database
 * @param $file
 * @param $locale
 * @param $mode
 * @return unknown_type
 */
function l10n_update_import_file($file, $locale, $mode = LOCALE_IMPORT_OVERWRITE) {   
  // If the file is a filepath, create a $file object
  if (is_string($file)) {
    $filepath = $file;
    $file = new Stdclass();
    $file->filepath = $filepath;
    $file->filename = $filepath;
  }
  return _l10n_update_locale_import_po($file, $locale, $mode , 'default');
}

/**
 * Get remote file and download it to a temporary path
 */
function l10n_update_download_file($download_url, $destination = NULL) {
  $t = get_t();
  $variables['%download_link'] = $download_url;
  
  $file = $destination ? $destination : tempnam(file_directory_temp(), 'translation-');

  if ($file) {
    $variables['%tmpfile'] = $file;
    if (($contents = drupal_http_request($download_url)) && file_put_contents($file, $contents->data)) {
      watchdog('l10n_update', 'Successfully downloaded %download_link to %tmpfile', $variables);
      return $file; 
    }
    else {
      watchdog('l10n_update', 'Unable to download and save %download_link file to %tmpfile.', $variables, WATCHDOG_ERROR);
    }
  }
  else {
    $variables['%tmpdir'] = file_directory_temp();
    watchdog('l10n_update', 'Error creating temporary file for download in %tmpdir. Remote file is %download_link.', $variables, WATCHDOG_ERROR);
  }  
}

/**
 * Get names for the language list from locale system
 */
function l10n_update_get_language_names($string_list) {
  $t = get_t();
  $language_codes = array_map('trim', explode(',', $string_list));
  $languages = _locale_get_predefined_list();
  $result = array();
  foreach ($language_codes as $lang) {      
    if (array_key_exists($lang, $languages)) {
      // Try to use verbose locale name
      $name = $lang;
      $name = $languages[$name][0] . (isset($languages[$name][1]) ? ' '. $t('(@language)', array('@language' => $languages[$name][1])) : '');
      $result[$lang] = $name;
    }
  }
  return $result;
}

/**
 * Get update information about single release
 */
function l10n_update_project_get_release($name, $langcode, $version, $server = NULL) {
  $projects[] = _l10n_update_build_project($name, $version, $server);
  if ($updates = _l10n_update_fetch_releases($projects, $langcode)) {
    return _l10n_update_project_release($updates[$name], $langcode, $version);
  }
}

/**
 * Compare and get list of downloadable updates
 */
 function _l10n_update_compare_multiple($projects, $history, $available) {
   $updates = array();
   foreach ($available as $name => $project_updates) {    
     foreach ($project_updates as $lang => $update) {
       if (!empty($projects[$name]) && !empty($update->timestamp) && (empty($history[$name][$lang]) || _l10n_update_compare($history[$name][$lang], $update))) {
         $updates[$name][$lang] = $update;
       }
     }
   }
   return $updates;
 }

/**
 * Compare current file with an update candidate
 * 
 * @return boolean
 *   TRUE if the file needs update
 */
function _l10n_update_compare($current, $update) {
  return !empty($update->timestamp) && 
    ($update->timestamp > $current->timestamp || $update->version != $current->version);
}

/**
 * Build project data as an object
 */
function _l10n_update_build_project($name, $version = NULL, $server = L10N_UPDATE_DEFAULT_SERVER, $path = L10N_UPDATE_DEFAULT_URL) {
  $project = new Stdclass();
  $project->name = $name;
  $project->version = $version;
  $project->l10n_server = $server;
  $project->l10n_path = $path;
  return $project;
}

/**
 * Check latest release for project, languages
 */
function l10n_update_check_projects($projects, $languages, $local = TRUE, $remote = TRUE) {
  $result = array();
  foreach ($projects as $name => $project) {
    foreach ($languages as $lang) {
      $source = l10n_update_build_source($project, $lang);
      if(l10n_update_check_source($source, $local, $remote)) {
        $result[$name][$lang] = $source;
      }
    }
  }
  return $result;
}

/**
 * Check file availability for project / language
 * 
 * @param $project
 *   Project object
 * @param $langcode
 *   Language code to check
 */
function l10n_update_check_translation($project, $langcode) {
  $url = l10n_update_build_url($project, $langcode);
  $result = l10n_update_http_check($url);
  // Add some project data to the result
  if ($result) {
    $result->language = $langcode;
    $result->version = $project->version;
  }
  return $result;
}

/**
 * Update the file history table
 * 
 * @param $file
 *   Object representing the file just imported or downloaded
 */
function l10n_update_file_history($file) {
  // Update or write new record
  if (db_result(db_query("SELECT project FROM {l10n_update_file} WHERE project = '%s' AND language = '%s'", $file->project, $file->language))) {
    $update = array('project', 'language');
  }
  else {
    $update = array();
  }
  return drupal_write_record('l10n_update_file', $file, $update);
}

/**
 * Check file existence and last updated time
 */
function l10n_update_http_check($url, $headers = array()) {
  $result = l10n_update_http_request($url, $headers, 'HEAD');
  if ($result && $result->code == '200') {
    $result->updated = isset($result->headers['Last-Modified']) ? strtotime($result->headers['Last-Modified']) : 0;
  }
  return $result;
}
/**
 * Perform an HTTP request. Installer safe simplified version.
 * 
 * We cannot use drupal_http_request() at install, see http://drupal.org/node/527484
 * 
 * This is a flexible and powerful HTTP client implementation. Correctly handles
 * GET, POST, PUT or any other HTTP requests. Handles redirects.
 *
 * @param $url
 *   A string containing a fully qualified URI.
 * @param $headers
 *   An array containing an HTTP header => value pair.
 * @param $method
 *   A string defining the HTTP request to use.
 * @param $data
 *   A string containing data to include in the request.
 * @param $retry
 *   An integer representing how many times to retry the request in case of a
 *   redirect.
 * @param $check
 *   Just check file existence and headers (update time)
 * @return
 *   An object containing the HTTP request headers, response code, headers,
 *   data and redirect status.
 */
function l10n_update_http_request($url, $headers = array(), $method = 'GET', $data = NULL, $retry = 3) {
  $result = new stdClass();
  // Return also full URL string
  $result->url = $url;
  // Parse the URL and make sure we can handle the schema.
  $uri = parse_url($url);

  if ($uri == FALSE) {
    $result->error = 'unable to parse URL';
    return $result;
  }

  if (!isset($uri['scheme'])) {
    $result->error = 'missing schema';
    return $result;
  }

  switch ($uri['scheme']) {
    case 'http':
      $port = isset($uri['port']) ? $uri['port'] : 80;
      $host = $uri['host'] . ($port != 80 ? ':'. $port : '');
      $fp = @fsockopen($uri['host'], $port, $errno, $errstr, 15);
      break;
    case 'https':
      // Note: Only works for PHP 4.3 compiled with OpenSSL.
      $port = isset($uri['port']) ? $uri['port'] : 443;
      $host = $uri['host'] . ($port != 443 ? ':'. $port : '');
      $fp = @fsockopen('ssl://'. $uri['host'], $port, $errno, $errstr, 20);
      break;
    default:
      $result->error = 'invalid schema '. $uri['scheme'];
      return $result;
  }

  // Make sure the socket opened properly.
  if (!$fp) {
    // When a network error occurs, we use a negative number so it does not
    // clash with the HTTP status codes.
    $result->code = -$errno;
    $result->error = trim($errstr);

    // Mark that this request failed. This will trigger a check of the web
    // server's ability to make outgoing HTTP requests the next time that
    // requirements checking is performed.
    // @see system_requirements()
    // variable_set('drupal_http_request_fails', TRUE);

    return $result;
  }

  // Construct the path to act on.
  $path = isset($uri['path']) ? $uri['path'] : '/';
  if (isset($uri['query'])) {
    $path .= '?'. $uri['query'];
  }

  // Create HTTP request.
  $defaults = array(
    // RFC 2616: "non-standard ports MUST, default ports MAY be included".
    // We don't add the port to prevent from breaking rewrite rules checking the
    // host that do not take into account the port number.
    'Host' => "Host: $host",
    'User-Agent' => 'User-Agent: Drupal (+http://drupal.org/)',
    'Content-Length' => 'Content-Length: '. strlen($data)
  );

  // If the server url has a user then attempt to use basic authentication
  if (isset($uri['user'])) {
    $defaults['Authorization'] = 'Authorization: Basic '. base64_encode($uri['user'] . (!empty($uri['pass']) ? ":". $uri['pass'] : ''));
  }

  // If the database prefix is being used by SimpleTest to run the tests in a copied
  // database then set the user-agent header to the database prefix so that any
  // calls to other Drupal pages will run the SimpleTest prefixed database. The
  // user-agent is used to ensure that multiple testing sessions running at the
  // same time won't interfere with each other as they would if the database
  // prefix were stored statically in a file or database variable.
  if (preg_match("/simpletest\d+/", $GLOBALS['db_prefix'], $matches)) {
    $defaults['User-Agent'] = 'User-Agent: ' . $matches[0];
  }

  foreach ($headers as $header => $value) {
    $defaults[$header] = $header .': '. $value;
  }

  $request = $method .' '. $path ." HTTP/1.0\r\n";
  $request .= implode("\r\n", $defaults);
  $request .= "\r\n\r\n";
  $request .= $data;

  $result->request = $request;

  fwrite($fp, $request);

  // Fetch response. If in test mode, don't fetch data;
  $response = '';
  while (!feof($fp) && $chunk = fread($fp, 1024)) {
    $response .= $chunk;
  }
  fclose($fp);

  // Parse response.
  list($split, $result->data) = explode("\r\n\r\n", $response, 2);
  $split = preg_split("/\r\n|\n|\r/", $split);

  list($protocol, $code, $text) = explode(' ', trim(array_shift($split)), 3);
  $result->headers = array();

  // Parse headers.
  while ($line = trim(array_shift($split))) {
    list($header, $value) = explode(':', $line, 2);
    if (isset($result->headers[$header]) && $header == 'Set-Cookie') {
      // RFC 2109: the Set-Cookie response header comprises the token Set-
      // Cookie:, followed by a comma-separated list of one or more cookies.
      $result->headers[$header] .= ','. trim($value);
    }
    else {
      $result->headers[$header] = trim($value);
    }
  }

  $responses = array(
    100 => 'Continue', 101 => 'Switching Protocols',
    200 => 'OK', 201 => 'Created', 202 => 'Accepted', 203 => 'Non-Authoritative Information', 204 => 'No Content', 205 => 'Reset Content', 206 => 'Partial Content',
    300 => 'Multiple Choices', 301 => 'Moved Permanently', 302 => 'Found', 303 => 'See Other', 304 => 'Not Modified', 305 => 'Use Proxy', 307 => 'Temporary Redirect',
    400 => 'Bad Request', 401 => 'Unauthorized', 402 => 'Payment Required', 403 => 'Forbidden', 404 => 'Not Found', 405 => 'Method Not Allowed', 406 => 'Not Acceptable', 407 => 'Proxy Authentication Required', 408 => 'Request Time-out', 409 => 'Conflict', 410 => 'Gone', 411 => 'Length Required', 412 => 'Precondition Failed', 413 => 'Request Entity Too Large', 414 => 'Request-URI Too Large', 415 => 'Unsupported Media Type', 416 => 'Requested range not satisfiable', 417 => 'Expectation Failed',
    500 => 'Internal Server Error', 501 => 'Not Implemented', 502 => 'Bad Gateway', 503 => 'Service Unavailable', 504 => 'Gateway Time-out', 505 => 'HTTP Version not supported'
  );
  // RFC 2616 states that all unknown HTTP codes must be treated the same as the
  // base code in their class.
  if (!isset($responses[$code])) {
    $code = floor($code / 100) * 100;
  }

  switch ($code) {
    case 200: // OK
    case 304: // Not modified
      break;
    case 301: // Moved permanently
    case 302: // Moved temporarily
    case 307: // Moved temporarily
      $location = $result->headers['Location'];

      if ($retry) {
        $result = l10n_update_http_request($result->headers['Location'], $headers, $method, $data, --$retry);
        $result->redirect_code = $result->code;
      }
      $result->redirect_url = $location;

      break;
    default:
      $result->error = $text;
  }

  $result->code = $code;
  return $result;
}

/**
 * Get a unique key for this site, not identifiable but usable to track the requests
 * from the same server.
 * 
 * We cannot use the private key before the database is created.
 * 
 * @return
 *   The site key.
 */
function l10n_update_get_site_key() {
  global $base_url;
  
  return md5('l10n_update' . $base_url . $_SERVER['SERVER_NAME'] . $_SERVER['SERVER_ADDR']);
}

/**
 * Populate an array of project data.
 */
function _l10n_update_process_info_list(&$projects, $list, $project_type) {
  foreach ($list as $file) {
    if (empty($file->status)) {
      // Skip disabled modules or themes.
      continue;
    }

    // Skip if the .info file is broken.
    if (empty($file->info)) {
      continue;
    }

    // If the .info doesn't define the 'project', try to figure it out.
    if (!isset($file->info['project'])) {
      $file->info['project'] = update_get_project_name($file);
    }

    // If we still don't know the 'project', give up.
    if (empty($file->info['project'])) {
      continue;
    }

    // If we don't already know it, grab the change time on the .info file
    // itself. Note: we need to use the ctime, not the mtime (modification
    // time) since many (all?) tar implementations will go out of their way to
    // set the mtime on the files it creates to the timestamps recorded in the
    // tarball. We want to see the last time the file was changed on disk,
    // which is left alone by tar and correctly set to the time the .info file
    // was unpacked.
    if (!isset($file->info['_info_file_ctime'])) {
      $info_filename = dirname($file->filename) .'/'. $file->name .'.info';
      $file->info['_info_file_ctime'] = filectime($info_filename);
    }

    $project_name = $file->info['project'];
    if (!isset($projects[$project_name])) {
      // Only process this if we haven't done this project, since a single
      // project can have multiple modules or themes.
      $projects[$project_name] = array(
        'name' => $project_name,
        'info' => $file->info,
        'datestamp' => isset($file->info['datestamp']) ? $file->info['datestamp'] : 0,
        'includes' => array($file->name => $file->info['name']),
        'type' => $project_name == 'drupal' ? 'core' : $project_type,
        'version' => isset($file->info['version']) ? $file->info['version'] : '',
        'core' => isset($file->info['core']) ? $file->info['core'] : '',
      );
    }
    else {
      $projects[$project_name]['includes'][$file->name] = $file->info['name'];
      $projects[$project_name]['info']['_info_file_ctime'] = max($projects[$project_name]['info']['_info_file_ctime'], $file->info['_info_file_ctime']);
    }
  }
}

/**
 * Build abstract translation source, to be mapped to a file or a download
 */
function l10n_update_build_source($project, $langcode, $filename = L10N_UPDATE_DEFAULT_FILENAME) {
  $source = clone $project;
  $source->project = $project->name;
  $source->language = $langcode;
  $source->filename = l10n_update_build_string($source, $filename);
  return $source;
}

/**
 * Build abstract translation source, to be mapped to a file or a download
 */
function l10n_update_build_string($project, $template) {
  $variables = array(
    '%project' => $project->name,
    '%release' => $project->version,
    // For core path replace dot by slash
    '%core' => strtr($project->core, array('.' => '-')),
    '%language' => isset($project->language) ? $project->language : '%language',
    '%filename' => isset($project->filename) ? $project->filename : '%filename',
  );
  return strtr($template, $variables);
}

/**
 * Check local and remote sources for the file
 */
function l10n_update_check_source($source, $local = TRUE, $remote = TRUE) {
  if ($local && $file = l10n_update_check_source_file($source)) {
    return $file;
  }
  if ($remote && $response = l10n_update_check_source_download($source)) {
    return $response;
  }
}

/**
 * Check download
 */
function l10n_update_check_source_download($source) {
  $url = l10n_update_build_string($source, $source->l10n_path);
  $result = l10n_update_http_check($url);
  if ($result && !empty($result->updated)) {
    $source->type = 'download';
    // There may have been redirects so we store the resulting url
    $source->fileurl = $result->url;
    $source->timestamp = $result->updated;
    return $result;
  } 
}

/**
 * Check whether we've got the file in the filesystem under 'translations'
 * 
 * It will search, similar to modules and themes:
 * - translations
 * - sites/all/translations
 * - sites/mysite/translations
 * 
 * Using name as the key will return just the last one found
 * 
 * @return Object
 *  File object if found (filename, basename, name)
 */
function l10n_update_check_source_file($source, $directory = 'translations') {
  $filename = preg_quote($source->filename) . '$';
  // Using the 'name' key will return 
  if ($files = drupal_system_listing($filename, $directory, 'name', 0)) {
    $file = current($files);
    $source->type = 'localfile';
    $source->filepath = $file->filename;
    $source->timestamp = filemtime($file->filename);
    return $file;
  }
}