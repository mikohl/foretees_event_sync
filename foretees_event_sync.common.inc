<?php

/**
 * @file
 * ForeTees Sync module common functions.
 */

/**
 * Convert object returned from ForeTees API into an array of valid events only.
 */
function _foretees_event_sync_filter_data($ft_events) {
  $event_array = [];
  $event_date = "";
  foreach ($ft_events as $chunk) {
    foreach ($chunk['foreTeesClubCalendarData'] as $day) {
      if ((is_array($day)) && (array_key_exists('dayOfMonth', $day))) {
        $event_date = strtotime($day['date']);
        /* Checking if this is an actual date or APi metadata */
        foreach ($day as $key => $event) {
          if (strpos($key, 'entry') !== FALSE) {
            /* Again, skipping the cruft */
            $event_array[] = [
              'activity_id'   => $event['activityId'],
              'event_id'      => $event['eventId'],
              'display_text'  => $event['displayText'],
              'time_provided' => ($event['time'] !== ''),
              'event_date'    => strtotime(date("Y-m-d", $event_date) . ' ' . $event['time']),
              'itinerary'     => $event['itinerary'],
              'activity_name' => $event['activityName'],
            ];
          }
        }
      }
    }
  }
  return $event_array;
}

/**
 * Load feed from ForeTees.
 */
function _foretees_event_sync_get() {
  /* Can only request 28 days of events, so have to make multiple calls */
  $event_feed = [];
  $today = strtotime('today');
  $start_date = strtotime('-30 days');
  $end_date = strtotime('+90 days');
  $feed_url = variable_get('foretees_sync_endpoint_url');
  $params = [
    'type'        => 'getArcisEventData',
    'activity_id' => '999',
    'clubcal'     => TRUE,
    'club_id'     => variable_get('foretees_sync_club_id'),
    'k'           => variable_get('foretees_sync_public_key'),
  ];
  while ($start_date < $end_date) {
    $params['req_sdate'] = date('Ymd', $start_date);
    $req_edate = strtotime('+28 days', $start_date);
    if ($end_date < $req_edate) {
      $params['req_edate'] = date('Ymd', $end_date);
    }
    else {
      $params['req_edate'] = date('Ymd', $req_edate);
    }
    $query = http_build_query($params);
    $ch = curl_init();
    curl_setopt_array($ch, [
      CURLOPT_URL => $feed_url . "?" . $query,
      CURLOPT_RETURNTRANSFER => TRUE,
    ]);

    $json = curl_exec($ch);
    curl_close($ch);
    $json = iconv('UTF-8', 'UTF-8//IGNORE', utf8_encode($json));
    $event_feed[] = json_decode($json, TRUE);
    $start_date = strtotime('+29 days', $start_date);
  }
  return $event_feed;
}

/**
 * Delete nodes for events that have been deleted in ForeTees.
 */
function foretees_event_sync_delete_nodes($deleted_events, &$context) {
  $context['results']['deleted-event'] = 0;
  foreach ($deleted_events as $event) {
    $nid = $event->nid;
    $node = node_load($nid);
    $foretees_event_id = $event->event_id;
    if (!empty($node)) {
      $event_date_field = field_get_items('node', $node, 'field_event_date');
      $event_date = strtotime($event_date_field[0]['value']);
      if ($event_date > strtotime("-30 days")) {
        node_delete($nid);
        $context['results']['deleted-event']++;
      }
      else {
        /* Leave past events alone and delete */
        $ewrapper = entity_metadata_wrapper('node', $node);
        $ewrapper->foretees_event_id->set(NULL);
        $ewrapper->save();
      }
    }
    $deleted_record = db_delete('foretees_events')
      ->condition('nid', $nid)
      ->execute();
  }
}

/**
 * Direct traffic for the event processing.
 */
function foretees_event_sync_process_events($event_item, $prev_synced, &$context) {
  global $user;

  /* Load the event_category taxonomy so we can create fields for the activityIds.*/
  $event_category_taxonomy = taxonomy_vocabulary_machine_name_load('event_category');
  $event_category_terms = entity_load('taxonomy_term', FALSE, array('vid' => $event_category_taxonomy->vid));
  $reverse_category_search = [];
  foreach ($event_category_terms as $tid => $category) {
    if (strlen($category->foretees_activity_id['und'][0]['value'])) {
      $activity_id = $category->foretees_activity_id['und'][0]['value'];
      $reverse_category_search[$activity_id] = $tid;
    }
  }

  $event_nid = '';
  if (!$event_item['event_id']) {
    /* Events with no event ID in the ForeTees feed are skipped */
    if (isset($context['results']['no_id'])) {
      $context['results']['no_id']++;
    }
    else {
      $context['results']['no_id'] = 1;
    }
  }
  else {
    /* ForeTees has supplied us an event ID, so we process the event */
    $foretees_event_id = $event_item['event_id'] . $event_item['activity_id'];
    if (array_key_exists($foretees_event_id, $prev_synced)) {
      /* Process existing events - update if difference are found */
      $event_nid = $prev_synced[$foretees_event_id]->nid;
      $event_updated = FALSE;
      $event_node = node_load($event_nid);
      $node_event_time_field = field_get_items('node', $event_node, 'field_event_date');
      $event_node_date = strtotime($node_event_time_field[0]['value']);
      $event_node_category = field_get_items('node', $event_node, 'field_event_cat');
      $event_node_body = field_get_items('node', $event_node, 'body');
      $event_node_event_id = field_get_items('node', $event_node, 'foretees_event_id');
      if ($event_node_category[0]['tid'] != $reverse_category_search[$event_item['activity_id']]) {
        $event_updated = TRUE;
      };
      if ($event_node_date !== $event_item['event_date']) {
        $event_updated = TRUE;
      };
      if ($event_node->title !== $event_item['display_text']) {
        $event_updated = TRUE;
      };
      if ($event_node_body[0]['value'] !== $event_item['itinerary']) {
        $event_updated = TRUE;
      }
      /* Check if the SSO link needs to be added */
      if (isset($event_item['activity_name']) && !($event_node_event_id)) {
        $event_updated = TRUE;
      }
      /* Check if the SSO link needs to be removed */
      if (!isset($event_item['activity_name']) && ($event_node_event_id)) {
        $event_updated = TRUE;
      }
      if ($event_updated) {
        $returned_nid = foretees_event_sync_write_event($event_item, $event_node, $reverse_category_search);
        if (isset($context['results']['updated'])) {
          $context['results']['updated']++;
        }
        else {
          $context['results']['updated'] = 1;
        }
      }
    }
    else {
      /* Create new events */
      $values = [
        'type' => 'events',
        'uid' => $user->uid,
        'status' => 1,
        'comment' => 1,
        'promote' => 0,
      ];
      $event_node = entity_create('node', $values);
      $returned_nid = foretees_event_sync_write_event($event_item, $event_node, $reverse_category_search);
      if (isset($context['results']['new'])) {
        $context['results']['new']++;
      }
      else {
        $context['results']['new'] = 1;
      }
    };
    if (isset($returned_nid) && ($returned_nid !== $event_nid)) {
      $foretees_update = db_insert('foretees_events')->fields(
        [
          'event_id' => $foretees_event_id,
          'nid' => $returned_nid,
          'activity_name' => $event_item['display_text'],
        ]
      )->execute();
    };
  }
}

/**
 * Batch operation to add new events to calender.
 */
function foretees_event_sync_write_event($event, $entity, $reverse_category_search) {
  $foretees_event_id = $event['event_id'] . $event['activity_id'];
  $ewrapper = entity_metadata_wrapper('node', $entity);
  $event_date = $event['event_date'];
  $event_category = $reverse_category_search[$event['activity_id']];
  $entity->field_event_date[LANGUAGE_NONE][0] = [
    'value' => date('Y-m-d H:i:s', $event_date),
    'value2' => date('Y-m-d H:i:s', $event_date),
    'timezone' => 'UTC',
    'timezone_db' => 'UTC',
  ];
  $entity->path['pathauto'] = TRUE;
  $ewrapper->title->set($event['display_text']);
  if (strlen($event_category) >= 1) {
    $ewrapper->field_event_cat->set($event_category);
  }
  else {
    $ewrapper->field_event_cat->set("70");
  }
  $ewrapper->field_event_status->set('alternate');
  if (isset($event['activity_name'])) {
    /* The activityName parameter is not sent for events that do not allow user
     * registration. We store the event ID as a signal to create the SSO link. */
    $ewrapper->foretees_event_id->set($foretees_event_id);
  }
  else {
    $ewrapper->foretees_event_id->set(NULL);
  };
  $ewrapper->body->set([
    'value' => $event['itinerary'],
    'format' => 'filtered_html',
  ]);
  $ewrapper->save();
  /* Pull the node ID to update the foretees_events table */
  $nid = $ewrapper->getIdentifier();
  return $nid;
}

/**
 * Sync the function in a batch from the settings form.
 */
function foretees_event_sync_settings_form_submit_sync() {
  if (!variable_get('foretees_syncing_now', FALSE)) {
    variable_set('foretees_syncing_now', TRUE);
    $syncdata = _foretees_event_sync_get();
    $event_array = _foretees_event_sync_filter_data($syncdata);
    $query = db_query("SELECT event_id, nid FROM {foretees_events}");
    $prev_synced = $missing_from_sync = $query->fetchAllAssoc('event_id');
    $max = count($event_array);
    $batch = [
      'operations' => [],
      'finished' => 'foretees_event_sync_batch_finished',
      'init_message' => 'Sync starting.',
      'progress_message' => t('Processing @total events from ForeTees', ['@total' => $max]),
      'error_message' => t('Sync has failed'),
      'file' => drupal_get_path('module', 'foretees_event_sync') . '/foretees_event_sync.common.inc',
    ];

    foreach ($event_array as $event) {
      $foretees_event_id = $event['event_id'] . $event['activity_id'];
      unset($missing_from_sync[$foretees_event_id]);
      $batch['operations'][] = [
        'foretees_event_sync_process_events',
        [
          $event,
          $prev_synced,
        ],
      ];
    }

    $batch['operations'][] = [
      'foretees_event_sync_delete_nodes',
      [
        $missing_from_sync,
      ],
    ];
    batch_set($batch);
    batch_process();
  }
  else {
    drupal_set_message(
      "Sync is currently running via cron or another user. Please wait a few minutes and try again. If the problem persists, contact Belo for support."
    );
  }
}

/**
 * What to do when the batch sync completes.
 */
function foretees_event_sync_batch_finished($success, $results, $operations) {
  if ($success) {
    if ($results['updated'] > 0) {
      drupal_set_message(
        t('@updated events updated.',
          [
            '@updated' => $results['updated'],
          ]),
        'status');
    };
    if ($results['new'] > 0) {
      drupal_set_message(
        t('@new events added.',
          [
            '@new' => $results['new'],
          ]),
        'status');
    };
    if ($results['deleted-event'] > 0) {
      drupal_set_message(
        t('@deleted events deleted.',
          [
            '@deleted' => $results['deleted-event'],
          ]),
        'status');
    };
    if ($results['no_id'] > 0) {
      drupal_set_message(
        t('@no_id events not imported--no event ID supplied in ForeTees.',
          [
            '@no_id' => $results['no_id'],
          ]),
        'status');
    }
  };
  drupal_set_message("Sync complete");
  variable_set('foretees_syncing_now', FALSE);
}

/**
 * Load the cron queue.
 */
function foretees_event_sync_load_queue() {
  $now = time();
  $sync_frequency = variable_get('foretees_sync_frequency', 24);
  $sync_seconds = $sync_frequency * 60 * 60;
  $last_sync = variable_get('foretees_last_sync', 0);
  $next_sync = $sync_seconds + $last_sync;
  if (($next_sync <= $now) && (!variable_get('foretees_syncing_now', FALSE))) {
    watchdog('foretees_event_sync', 'Foretees Event Sync starting via cron.');
    variable_set('foretees_syncing_now', TRUE);
    $syncdata = _foretees_event_sync_get();
    $event_array = _foretees_event_sync_filter_data($syncdata);
    $query = db_query("SELECT event_id, nid FROM {foretees_events}");
    $prev_synced = $missing_from_sync = $query->fetchAllAssoc('event_id');
    $max = count($event_array);
    $queue = DrupalQueue::get("foretees_sync");
    foreach ($event_array as $event) {
      $queue_item = [
        'event' => $event,
        'prev_synced' => $prev_synced,
      ];
      $foretees_event_id = $event['event_id'] . $event['activity_id'];
      unset($missing_from_sync[$foretees_event_id]);
      $queue->createItem($queue_item);
    }
    $cleanup_queue_item = [
      'missing_from_sync' => $missing_from_sync,
    ];
    $queue->createItem($cleanup_queue_item);
    variable_set('foretees_last_sync', $now);
  }
}

/**
 * Process queue items.
 */
function foretees_event_sync_cron_queue_worker($item) {
  $context = [];
  if (array_key_exists('missing_from_sync', $item)) {
    $missing_from_sync = $item['missing_from_sync'];
    foretees_event_sync_delete_nodes($missing_from_sync, $context);
    watchdog('foretees_event_sync', 'Foretees Event Sync complete.');
    variable_set('foretees_syncing_now', FALSE);
  }
  else {
    $event = $item['event'];
    $prev_synced = $item['prev_synced'];
    foretees_event_sync_process_events($event, $prev_synced, $context);
  };
}
