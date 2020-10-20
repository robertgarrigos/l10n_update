<?php

/**
 * @file
 * Definition of TranslationStreamWrapper.
 */

/**
 * A Backdrop interface translations (translations://) stream wrapper class.
 *
 * Supports storing translation files.
 */
class TranslationsStreamWrapper extends BackdropLocalStreamWrapper {
  /**
   * Implements abstract public function getDirectoryPath().
   */
  public function getDirectoryPath() {
    return config_get('l10n_update.settings', 'l10n_update_download_store');
  }

  /**
   * Overrides getExternalUrl().
   */
  public function getExternalUrl() {
    throw new Exception('PO files URL should not be public.');
  }

}
