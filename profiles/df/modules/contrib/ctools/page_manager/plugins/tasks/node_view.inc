<?php

/**
 * @file
 * Handle the 'node view' override task.
 *
 * This plugin overrides node/%node and reroutes it to the page manager, where
 * a list of tasks can be used to service this request based upon criteria
 * supplied by access plugins.
 */

/**
 * Specialized implementation of hook_page_manager_task_tasks(). See api-task.html for
 * more information.
 */
function page_manager_node_view_page_manager_tasks() {
  return array(
    // This is a 'page' task and will fall under the page admin UI
    'task type' => 'page',

    'title' => t('Node template'),

    'admin title' => t('Node template'),
    'admin description' => t('When enabled, this overrides the default Drupal behavior for displaying nodes at <em>node/%node</em>. If you add variants, you may use selection criteria such as node type or language or user access to provide different views of nodes. If no variant is selected, the default Drupal node view will be used. This page only affects nodes viewed as pages, it will not affect nodes viewed in lists or at other locations. Also please note that if you are using pathauto, aliases may make a node to be somewhere else, but as far as Drupal is concerned, they are still at node/%node.'),
    'admin path' => 'node/%node',

    // Menu hooks so that we can alter the node/%node menu entry to point to us.
    'hook menu' => 'page_manager_node_view_menu',
    'hook menu alter' => 'page_manager_node_view_menu_alter',

    // This is task uses 'context' handlers and must implement these to give the
    // handler data it needs.
    'handler type' => 'context',
    'get arguments' => 'page_manager_node_view_get_arguments',
    'get context placeholders' => 'page_manager_node_view_get_contexts',

    // Allow this to be enabled or disabled:
    'disabled' => variable_get('page_manager_node_view_disabled', TRUE),
    'enable callback' => 'page_manager_node_view_enable',
    'access callback' => 'page_manager_node_view_access_check',
  );
}

/**
 * Callback defined by page_manager_node_view_page_manager_tasks().
 *
 * Alter the node view input so that node view comes to us rather than the
 * normal node view process.
 */
function page_manager_node_view_menu_alter(&$items, $task) {
  if (variable_get('page_manager_node_view_disabled', TRUE)) {
    return;
  }

  // Override the node view handler for our purpose.
  $callback = $items['node/%node']['page callback'];
  if ($callback == 'node_page_view' || variable_get('page_manager_override_anyway', FALSE)) {
    $items['node/%node']['page callback'] = 'page_manager_node_view_page';
    $items['node/%node']['file path'] = $task['path'];
    $items['node/%node']['file'] = $task['file'];
  }
  else {
    // automatically disable this task if it cannot be enabled.
    variable_set('page_manager_node_view_disabled', TRUE);
    if (!empty($GLOBALS['page_manager_enabling_node_view'])) {
      drupal_set_message(t('Page manager module is unable to enable node/%node because some other module already has overridden with %callback.', array('%callback' => $callback)), 'error');
    }
  }

  // @todo override node revision handler as well?
}

/**
 * Entry point for our overridden node view.
 *
 * This function asks its assigned handlers who, if anyone, would like
 * to run with it. If no one does, it passes through to Drupal core's
 * node view, which is node_page_view().
 */
function page_manager_node_view_page($node) {
  // Load my task plugin
  $task = page_manager_get_task('node_view');

  // Load the node into a context.
  ctools_include('context');
  ctools_include('context-task-handler');

  // We need to mimic Drupal's behavior of setting the node title here.
  drupal_set_title($node->title);
  $uri = entity_uri('node', $node);
  // Set the node path as the canonical URL to prevent duplicate content.
  drupal_add_html_head_link(array('rel' => 'canonical', 'href' => url($uri['path'], $uri['options'])), TRUE);
  // Set the non-aliased path as a default shortlink.
  drupal_add_html_head_link(array('rel' => 'shortlink', 'href' => url($uri['path'], array_merge($uri['options'], array('alias' => TRUE)))), TRUE);

  // Load all contexts.
  $contexts = ctools_context_handler_get_task_contexts($task, '', array($node));
  // Build the full output using the configured CTools plugin.
  $output = ctools_context_handler_render($task, '', $contexts, array($node->nid));
  if ($output !== FALSE) {
    node_tag_new($node);
  }
  else {
    // Otherwise try loading an override plugin with a node_page_view()
    // fallback.
    $function = 'node_page_view';
    foreach (module_implements('page_manager_override') as $module) {
      $call = $module . '_page_manager_override';
      if (($rc = $call('node_view')) && function_exists($rc)) {
        $function = $rc;
        break;
      }
    }
    $output = $function($node);
  }

  // Finally, we need to test if the entity view hooks were invoked for this
  // node already, and invoke them manually if not.
  $viewed_entities = drupal_static('ctools_entity_view');
  if (!isset($viewed_entities['node'][$node->nid])) {
    node_show($node); // Quick way to invoke ALL view hooks.
  }

  return $output;
}

/**
 * Callback to get arguments provided by this task handler.
 *
 * Since this is the node view and there is no UI on the arguments, we
 * create dummy arguments that contain the needed data.
 */
function page_manager_node_view_get_arguments($task, $subtask_id) {
  return array(
    array(
      'keyword' => 'node',
      'identifier' => t('Node being viewed'),
      'id' => 1,
      'name' => 'entity_id:node',
      'settings' => array(),
    ),
  );
}

/**
 * Callback to get context placeholders provided by this handler.
 */
function page_manager_node_view_get_contexts($task, $subtask_id) {
  return ctools_context_get_placeholders_from_argument(page_manager_node_view_get_arguments($task, $subtask_id));
}

/**
 * Callback to enable/disable the page from the UI.
 */
function page_manager_node_view_enable($cache, $status) {
  variable_set('page_manager_node_view_disabled', $status);

  // Set a global flag so that the menu routine knows it needs
  // to set a message if enabling cannot be done.
  if (!$status) {
    $GLOBALS['page_manager_enabling_node_view'] = TRUE;
  }
}

/**
 * Callback to determine if a page is accessible.
 *
 * @param $task
 *   The task plugin.
 * @param $subtask_id
 *   The subtask id
 * @param $contexts
 *   The contexts loaded for the task.
 * @return
 *   TRUE if the current user can access the page.
 */
function page_manager_node_view_access_check($task, $subtask_id, $contexts) {
  $context = reset($contexts);
  return node_access('view', $context->data);
}
