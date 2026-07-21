import yaml
import os
from copy import deepcopy

DEFAULT_CONFIG_FILE_PATH = 'config.yaml'

def load_raw_config_from_file(config_path=DEFAULT_CONFIG_FILE_PATH):
    """Loads raw configuration from YAML file."""
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"Configuration file not found: {config_path}")
    with open(config_path, 'r', encoding='utf-8') as f:
        raw_config = yaml.safe_load(f)
    if raw_config is None:
        raw_config = {}
    return raw_config

def process_configs(raw_config):
    """
    Processes raw configuration, applying default settings.
    Includes conversion of color lists to tuples in various places.
    """
    default_pipeline_settings = raw_config.get('default_pipeline_settings', {})
    processed_pipelines_list = []

    for p_conf_override in raw_config.get('pipelines', []):
        final_pipeline_conf = deepcopy(default_pipeline_settings)

        for key, value in p_conf_override.items():
            if isinstance(value, dict) and key in final_pipeline_conf and isinstance(final_pipeline_conf[key], dict):
                merged_dict = deepcopy(final_pipeline_conf[key])
                merged_dict.update(value)
                final_pipeline_conf[key] = merged_dict
            else:
                final_pipeline_conf[key] = value

        required_keys = ['name', 'esp32_port', 'target_width', 'target_height', 'image_source_mode']
        pipeline_name_for_error = final_pipeline_conf.get('name', str(p_conf_override))
        for key in required_keys:
            if key not in final_pipeline_conf:
                raise ValueError(f"Missing required key '{key}' in pipeline configuration: '{pipeline_name_for_error}'")

        if 'wb_scale' in final_pipeline_conf and isinstance(final_pipeline_conf['wb_scale'], list):
             if len(final_pipeline_conf['wb_scale']) == 3:
                  final_pipeline_conf['wb_scale'] = tuple(final_pipeline_conf['wb_scale'])
             else:
                  print(f"[WARN][ConfigLoader] Invalid wb_scale length for '{pipeline_name_for_error}': {final_pipeline_conf['wb_scale']}. Using default (1,1,1).")
                  if 'wb_scale' in default_pipeline_settings and isinstance(default_pipeline_settings['wb_scale'], tuple):
                      final_pipeline_conf['wb_scale'] = default_pipeline_settings['wb_scale']
                  else:
                      final_pipeline_conf['wb_scale'] = (1.0, 1.0, 1.0)

        if 'prometheus_colors' in final_pipeline_conf and isinstance(final_pipeline_conf['prometheus_colors'], dict):
            for color_key, color_value in final_pipeline_conf['prometheus_colors'].items():
                if isinstance(color_value, list) and len(color_value) in [3, 4]:
                    final_pipeline_conf['prometheus_colors'][color_key] = tuple(color_value)

        if 'prometheus_metric_config' in final_pipeline_conf and isinstance(final_pipeline_conf['prometheus_metric_config'], dict):
            for metric_key, metric_details in final_pipeline_conf['prometheus_metric_config'].items():
                if isinstance(metric_details, dict):
                    for color_attr in ['color', 'color_read', 'color_write']:
                         if color_attr in metric_details and isinstance(metric_details[color_attr], list):
                             if len(metric_details[color_attr]) in [3, 4]:
                                 metric_details[color_attr] = tuple(metric_details[color_attr])
                             else:
                                 print(f"[WARN][ConfigLoader] Invalid color length for '{color_attr}' in metric '{metric_key}' of pipeline '{pipeline_name_for_error}': {metric_details[color_attr]}. Color will not be converted.")

        processed_pipelines_list.append(final_pipeline_conf)

    global_settings = raw_config.get('global_settings', {})
    return global_settings, processed_pipelines_list

def get_app_config(config_path=DEFAULT_CONFIG_FILE_PATH):
    """Main function for loading and processing application configuration."""
    raw_config = load_raw_config_from_file(config_path)
    global_settings, processed_pipelines = process_configs(raw_config)
    return global_settings, processed_pipelines

if __name__ == '__main__':
    try:
        gs, pipelines = get_app_config()
        print("Global Settings:", gs)
        for p_idx, p_conf in enumerate(pipelines):
            print(f"\nPipeline {p_idx + 1} ('{p_conf.get('name')}'):")
            for key, value in p_conf.items():
                print(f"  {key}: {value}")

    except Exception as e:
        print(f"Error loading/processing configuration: {e}")