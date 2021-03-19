<?php

/**
 * @file
 * ForeTees Sync module admin form.
 */

/**
 * Create settings form for sync module.
 */
function foretees_event_sync_settings_form($form, &$form_state) {
  /* Load common functions include file */
  $file_path = module_load_include('inc', 'foretees_event_sync', 'foretees_event_sync.common');

  /* Load the event_category taxonomy so we can create fields for the activityIds.*/
  $event_category_taxonomy = taxonomy_vocabulary_machine_name_load('event_category');
  $event_category_terms = entity_load('taxonomy_term', FALSE, array('vid' => $event_category_taxonomy->vid));

  /* Module settings form */
  $form['sync_fieldset'] = [
    '#type' => 'fieldset',
    '#title' => t("ForeTees Event Sync Settings"),
    '#tree' => TRUE,
  ];

  $form['sync_fieldset']['foretees_sync_endpoint_url'] = [
    '#type' => 'textfield',
    '#title' => t("API Endpoint URL from ForeTees"),
    '#disabled' => TRUE,
    '#default_value' => variable_get('foretees_sync_endpoint_url', 'http://www.example.com/v5/servlet/Common_webapi'),
    '#required' => TRUE,
  ];

  $form['sync_fieldset']['foretees_sync_public_key'] = [
    '#type' => 'textfield',
    '#title' => 'Public Key',
    '#disabled' => TRUE,
    '#default_value' => variable_get('foretees_sync_public_key', 'XXXXX'),
    '#required' => TRUE,
  ];

  $form['sync_fieldset']['foretees_sync_club_id'] = [
    '#type' => 'textfield',
    '#title' => t("Club ID from ForeTees"),
    '#default_value' => variable_get('foretees_sync_club_id', '000'),
    '#required' => TRUE,
  ];

  $form['sync_fieldset']['foretees_event_category_settings'] = [
    '#type' => 'fieldset',
    '#title' => t("Category Settings"),
    '#tree' => TRUE,
  ];

  $form['sync_fieldset']['foretees_event_category_settings']['content'] = [
    '#markup' => t("<p>Enter the Activity ID number that corresponds with each event category below.</p>"),
  ];

  foreach ($event_category_terms as $category) {
    $this_category_id = "foretees_event_category_" . $category->tid;
    $form['sync_fieldset']['foretees_event_category_settings'][$this_category_id] = [
      '#type' => 'textfield',
      '#title' => $category->name,
      '#default_value' => $category->foretees_activity_id['und'][0]['value'],
    ];
  }

  $form['sync_fieldset']['foretees_sync_active'] = [
    '#type' => 'select',
    '#options' => [
      0 => "No",
      1 => "Yes",
    ],
    '#default_value' => variable_get('foretees_sync_active', "0"),
    '#title' => 'Enable Automatic Syncing',
    '#required' => TRUE,
  ];

  $form['sync_fieldset']['foretees_sync_frequency'] = [
    '#type' => 'select',
    '#default_value' => variable_get('foretees_sync_frequency', 24),
    '#options' => [
      '1' => t('1 Hour'),
      '3' => t('3 Hours'),
      '6' => t('6 Hours'),
      '12' => t('12 Hours'),
      '24' => t('24 Hours'),
    ],
    '#title' => t('Sync every:'),
  ];

  $form['sync_fieldset']['foretees_sync_save'] = [
    '#type' => 'submit',
    '#value' => t("Save"),
  ];
  
  $form['execute_fieldset'] = [
    '#type' => 'fieldset',
    '#title' => t("Execute ForeTees Event Sync"),
    '#tree' => TRUE,
  ];

  $form['execute_fieldset']['foretees_sync_sync_now'] = [
    '#type' => 'submit',
    '#value' => t("Sync Now"),
    '#submit' => ['foretees_event_sync_settings_form_submit_sync'],
  ];

  return $form;
}

/**
 * Save form settings to variable_set.
 */
function foretees_event_sync_settings_form_submit($form, &$form_state) {
  $endpoint_url = $form_state['values']['sync_fieldset']['foretees_sync_endpoint_url'];
  $public_key = $form_state['values']['sync_fieldset']['foretees_sync_public_key'];
  $club_id = $form_state['values']['sync_fieldset']['foretees_sync_club_id'];
  $sync_active = $form_state['values']['sync_fieldset']['foretees_sync_active'];
  $sync_frequency = $form_state['values']['sync_fieldset']['foretees_sync_frequency'];
  $event_categories = $form_state['values']['sync_fieldset']['foretees_event_category_settings'];
  foreach ($event_categories as $event_category => $activity_id) {
    $tid = str_replace("foretees_event_category_", "", $event_category);
    $term = taxonomy_term_load($tid);
    $term->foretees_activity_id['und'][0]['value'] = $activity_id;
    taxonomy_term_save($term);
  }
  variable_set("foretees_sync_endpoint_url", $endpoint_url);
  variable_set("foretees_sync_public_key", $public_key);
  variable_set("foretees_sync_club_id", $club_id);
  variable_set("foretees_sync_active", $sync_active);
  variable_set("foretees_sync_frequency", $sync_frequency);
  drupal_set_message("Settings saved");
}
